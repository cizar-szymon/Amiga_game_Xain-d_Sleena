/* Lockstep test of the 6809 core against a MAME trace (tools/mame_capture.py --trace).
 *
 *   trace6809 CPU ROMDIR TRACE.txt [MAXERR]
 *     CPU: main | sub | sound
 *     TRACE.txt: trace_<cpu>.txt; the memory log trace_<cpu>_mem.txt is read next to it
 *
 * Before every instruction the registers and the cycle counter must equal the trace line.
 * ROM is read from the ROM files; every other read takes its value from the MAME memory log
 * (the next logged read of the same address), so I/O, shared RAM written by the other CPUs
 * and all RAM contents come out exactly as in MAME. Writes must match the log in order.
 * Interrupts: MAME writes "(interrupted at XXXX, IRQ n)" lines; the matching line is raised for
 * one step there.
 */
#include "m6809.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TraceLine {
    uint8_t a, b, dp, cc;
    uint16_t x, y, u, s, pc;
    uint32_t t;
    char text[64];
} TraceLine;

typedef struct MemEntry {
    char kind;
    uint16_t addr;
    uint8_t data;
    uint16_t pc;
    uint8_t used;
} MemEntry;

static uint8_t rom_fixed[0x8000];     /* 8000-FFFF */
static uint8_t rom_bank[2][0x4000];   /* 4000-7FFF */
static int bank;
static int cpu_kind;                  /* 0 main, 1 sub, 2 sound */

static MemEntry *mem;
static long nmem, mpos;
static long errors, maxerr = 20;
static long cur_line;

static void fail(const char *fmt, const char *a)
{
    fprintf(stderr, fmt, a);
    exit(2);
}

static int load(const char *dir, const char *name, uint8_t *dst, long off, long len)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) fail("cannot open %s\n", path);
    fseek(f, off, SEEK_SET);
    if (fread(dst, 1, (size_t)len, f) != (size_t)len) fail("short read %s\n", path);
    fclose(f);
    return 0;
}

static uint8_t rd(void *ctx, uint16_t a)
{
    long i, lim;
    (void)ctx;
    if (a >= 0x8000) return rom_fixed[a - 0x8000];
    if (cpu_kind != 2 && a >= 0x4000) return rom_bank[bank][a - 0x4000];
    if (cpu_kind == 2 && a >= 0x4000) return 0x00;   /* sound ROM region 4000-7FFF is empty */
    lim = mpos + 4000 < nmem ? mpos + 4000 : nmem;
    for (i = mpos; i < lim; i++) {
        if (mem[i].kind == 'R' && mem[i].addr == a && !mem[i].used) {
            mem[i].used = 1;
            mpos = i + 1;
            return mem[i].data;
        }
        if (mem[i].kind == 'W' && !mem[i].used) break;   /* reads never pass a pending write */
    }
    if (errors++ < maxerr)
        printf("line %ld: read %04X not found in memory log\n", cur_line, a);
    return 0xff;
}

static void wr(void *ctx, uint16_t a, uint8_t v)
{
    long i;
    (void)ctx;
    if (cpu_kind == 0 && a == 0x3a0f) bank = (v >> 3) & 1;
    if (cpu_kind == 1 && a == 0x3000) bank = v & 1;
    for (i = mpos; i < nmem; i++)
        if (mem[i].kind == 'W' && !mem[i].used) break;
    if (i >= nmem) {
        if (errors++ < maxerr) printf("line %ld: write %04X=%02X, log has no more writes\n", cur_line, a, v);
        return;
    }
    mem[i].used = 1;
    mpos = i + 1;
    if ((mem[i].addr != a || mem[i].data != v) && errors++ < maxerr)
        printf("line %ld: write %04X=%02X, MAME wrote %04X=%02X (pc %04X)\n",
               cur_line, a, v, mem[i].addr, mem[i].data, mem[i].pc);
}

static int parse_line(const char *s, TraceLine *t)
{
    unsigned a, b, x, y, u, sp, dp, cc, pc;
    unsigned long tt;
    const char *p;
    if (sscanf(s, "A=%x B=%x X=%x Y=%x U=%x S=%x DP=%x CC=%x T=%lu %x:",
               &a, &b, &x, &y, &u, &sp, &dp, &cc, &tt, &pc) != 10)
        return 0;
    t->a = (uint8_t)a; t->b = (uint8_t)b; t->x = (uint16_t)x; t->y = (uint16_t)y;
    t->u = (uint16_t)u; t->s = (uint16_t)sp; t->dp = (uint8_t)dp; t->cc = (uint8_t)cc;
    t->t = (uint32_t)tt; t->pc = (uint16_t)pc;
    p = strchr(s + 60, ':');
    snprintf(t->text, sizeof t->text, "%s", p ? p + 2 : "");
    t->text[strcspn(t->text, "\r\n")] = 0;
    return 1;
}

static void load_memlog(const char *trace)
{
    char path[1024], line[128];
    FILE *f;
    long cap = 1 << 16;
    size_t n = strlen(trace);
    if (n < 4) fail("bad trace name %s\n", trace);
    snprintf(path, sizeof path, "%.*s_mem.txt", (int)(n - 4), trace);
    f = fopen(path, "r");
    if (!f) fail("cannot open %s\n", path);
    mem = malloc(sizeof(MemEntry) * (size_t)cap);
    while (fgets(line, sizeof line, f)) {
        unsigned a, d, pc;
        char k;
        if (sscanf(line, "%c %x %x %x", &k, &a, &d, &pc) != 4) continue;
        if (nmem == cap) {
            cap *= 2;
            mem = realloc(mem, sizeof(MemEntry) * (size_t)cap);
        }
        mem[nmem].kind = k;
        mem[nmem].addr = (uint16_t)a;
        mem[nmem].data = (uint8_t)d;
        mem[nmem].pc = (uint16_t)pc;
        mem[nmem].used = 0;
        nmem++;
    }
    fclose(f);
}

static void set_regs(M6809 *c, const TraceLine *t)
{
    c->a = t->a; c->b = t->b; c->x = t->x; c->y = t->y; c->u = t->u; c->s = t->s;
    c->dp = t->dp; c->cc = t->cc; c->pc = t->pc;
}

static int same_regs(const M6809 *c, const TraceLine *t)
{
    return c->a == t->a && c->b == t->b && c->x == t->x && c->y == t->y && c->u == t->u &&
           c->s == t->s && c->dp == t->dp && c->cc == t->cc && c->pc == t->pc;
}

int main(int argc, char **argv)
{
    static M6809 cpu;
    M6809 *c = &cpu;
    FILE *tf;
    char line[256];
    TraceLine prev, cur;
    long n = 0, ints = 0, cyc_err = 0;
    int i, have_prev = 0, step_due = 0;

    if (argc < 4) {
        fprintf(stderr, "usage: trace6809 main|sub|sound ROMDIR TRACE.txt [MAXERR] [BANK]\n");
        return 2;
    }
    if (!strcmp(argv[1], "main")) {
        cpu_kind = 0;
        load(argv[2], "p9-08.ic66", rom_fixed, 0, 0x8000);
        load(argv[2], "pa-09.ic65", rom_bank[0], 0, 0x4000);
        load(argv[2], "pa-09.ic65", rom_bank[1], 0x4000, 0x4000);
    } else if (!strcmp(argv[1], "sub")) {
        cpu_kind = 1;
        load(argv[2], "p1-0.ic29", rom_fixed, 0, 0x8000);
        load(argv[2], "p0-0.ic15", rom_bank[0], 0, 0x4000);
        load(argv[2], "p0-0.ic15", rom_bank[1], 0x4000, 0x4000);
    } else {
        cpu_kind = 2;   /* sound: 4000-7FFF is unused (ROM is 32 KB at 8000) */
        load(argv[2], "p2-0.ic49", rom_fixed, 0, 0x8000);
    }
    if (argc > 4) maxerr = atol(argv[4]);
    if (argc > 5) bank = atoi(argv[5]);
    load_memlog(argv[3]);
    tf = fopen(argv[3], "r");
    if (!tf) fail("cannot open %s\n", argv[3]);

    m6809_init(c);
    c->read = rd;
    c->write = wr;
    c->lds_seen = 1;
    for (i = 0x80; i < 0x100; i++) c->rpage[i] = rom_fixed + (i - 0x80) * 256;

    /* an instruction is executed only when the trace has a following line, so the last one
       (whose memory accesses may be missing from the log) is not checked */
    while (fgets(line, sizeof line, tf)) {
        cur_line++;
        if (have_prev && (strstr(line, "(interrupted at") || strstr(line, "T="))) {
            if (step_due) {
                c->icount = 1 << 30;
                m6809_step(c);
                n++;
                step_due = 0;
            }
        }
        if (have_prev && strstr(line, "(interrupted at")) {
            /* MAME took an interrupt here: "IRQ 0" = IRQ line, "IRQ 1" = FIRQ line, else NMI */
            const char *q = strstr(line, ", ");
            if (q && !strncmp(q + 2, "IRQ 0", 5)) c->irq_line = 1;
            else if (q && !strncmp(q + 2, "IRQ 1", 5)) c->firq_line = 1;
            else c->nmi_pending = 1;
            c->icount = 1 << 30;
            m6809_step(c);
            c->firq_line = c->irq_line = 0;
            ints++;
            continue;
        }
        if (!parse_line(line, &cur)) continue;
        if (!have_prev) {
            /* the log may start with the end of an instruction or interrupt entry that MAME
               interrupted when the trace began: start at the debugger's first read of the
               first traced instruction (made exactly at the instruction boundary) */
            while (mpos < nmem && !(mem[mpos].kind == 'R' && mem[mpos].addr == cur.pc && mem[mpos].pc == cur.pc)) mpos++;
            set_regs(c, &cur);
            prev = cur;
            have_prev = 1;
            c->total = cur.t;
            step_due = 1;
            continue;
        }
        if (!same_regs(c, &cur)) {
            if (errors++ < maxerr)
                printf("line %ld after %04X %s:\n  core A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X PC=%04X\n"
                       "  MAME A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X PC=%04X\n",
                       cur_line, prev.pc, prev.text, c->a, c->b, c->x, c->y, c->u, c->s, c->dp, c->cc, c->pc,
                       cur.a, cur.b, cur.x, cur.y, cur.u, cur.s, cur.dp, cur.cc, cur.pc);
            set_regs(c, &cur);
        }
        if (c->total != cur.t) {
            if (cyc_err++ < maxerr)
                printf("line %ld after %04X %s: cycles %lu, MAME %lu (instruction %ld, MAME %ld)\n",
                       cur_line, prev.pc, prev.text, (unsigned long)c->total, (unsigned long)cur.t,
                       (long)(c->total - prev.t), (long)(cur.t - prev.t));
            c->total = cur.t;
        }
        prev = cur;
        step_due = 1;
    }
    fclose(tf);
    printf("%s: %ld instructions, %ld interrupts, %ld errors, %ld cycle errors\n",
           argv[3], n, ints, errors, cyc_err);
    return errors || cyc_err ? 1 : 0;
}
