/* Lockstep test of the 6805 core against a MAME trace of the MCU (mame_capture.py --trace mcu:mcu:F:T).
 *
 *   trace6805 ROMDIR TRACE.txt [MAXERR]
 *
 * Same method as trace6809: registers and cycle counter checked before every instruction, ROM
 * (pz-0.113 + bootstrap.bin) read from files, every other read served from the MAME memory log,
 * writes compared in order, interrupts raised where MAME marks them ("IRQ 0" = /INT, "IRQ 1" = timer).
 * The /INT pin (BIL/BIH) is modelled as in the Taito interface: low from the host write (IRQ)
 * until the MCU sets PB1.
 */
#include "m6805.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TraceLine {
    uint8_t a, x, s, cc;
    uint16_t pc;
    uint32_t t;
    char text[48];
} TraceLine;

typedef struct MemEntry {
    char kind;
    uint16_t addr, pc;
    uint8_t data, used;
} MemEntry;

static uint8_t rom[0x800];
static MemEntry *mem;
static long nmem, mpos, errors, maxerr = 20, cur_line;

static void fail(const char *fmt, const char *a)
{
    fprintf(stderr, fmt, a);
    exit(2);
}

static void load(const char *dir, const char *name, uint8_t *dst, long len)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) fail("cannot open %s\n", path);
    if (fread(dst, 1, (size_t)len, f) != (size_t)len) fail("short read %s\n", path);
    fclose(f);
}

static uint8_t rd(void *ctx, uint16_t a)
{
    long i, lim;
    (void)ctx;
    if (a >= 0x80) return rom[a];
    lim = mpos + 4000 < nmem ? mpos + 4000 : nmem;
    for (i = mpos; i < lim; i++) {
        if (mem[i].kind == 'R' && mem[i].addr == a && !mem[i].used) {
            mem[i].used = 1;
            mpos = i + 1;
            return mem[i].data;
        }
        if (mem[i].kind == 'W' && !mem[i].used) break;
    }
    if (errors++ < maxerr) printf("line %ld: read %03X not found in memory log\n", cur_line, a);
    return 0xff;
}

static M6805 cpu;

static void wr(void *ctx, uint16_t a, uint8_t v)
{
    long i;
    (void)ctx;
    /* Taito interface: PB1 high clears the host flag and with it the /INT line */
    if (a == 0x01 && (v & 0x02)) cpu.irq_pin = 0;
    for (i = mpos; i < nmem; i++)
        if (mem[i].kind == 'W' && !mem[i].used) break;
    if (i >= nmem) {
        if (errors++ < maxerr) printf("line %ld: write %03X=%02X, log has no more writes\n", cur_line, a, v);
        return;
    }
    mem[i].used = 1;
    mpos = i + 1;
    if ((mem[i].addr != a || mem[i].data != v) && errors++ < maxerr)
        printf("line %ld: write %03X=%02X, MAME wrote %03X=%02X\n", cur_line, a, v, mem[i].addr, mem[i].data);
}

static int parse_line(const char *s, TraceLine *t)
{
    unsigned a, x, sp, cc, pc;
    unsigned long tt;
    const char *p;
    if (sscanf(s, "A=%x X=%x S=%x CC=%x T=%lu %x:", &a, &x, &sp, &cc, &tt, &pc) != 6) return 0;
    t->a = (uint8_t)a; t->x = (uint8_t)x; t->s = (uint8_t)sp; t->cc = (uint8_t)cc;
    t->t = (uint32_t)tt; t->pc = (uint16_t)pc;
    p = strchr(strstr(s, "T="), ':');
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
    snprintf(path, sizeof path, "%.*s_mem.txt", (int)(n - 4), trace);
    f = fopen(path, "r");
    if (!f) fail("cannot open %s\n", path);
    mem = malloc(sizeof(MemEntry) * (size_t)cap);
    while (fgets(line, sizeof line, f)) {
        unsigned a, d, pc;
        char k;
        if (sscanf(line, "%c %x %x %x", &k, &a, &d, &pc) != 4) continue;
        if (nmem == cap) { cap *= 2; mem = realloc(mem, sizeof(MemEntry) * (size_t)cap); }
        mem[nmem].kind = k; mem[nmem].addr = (uint16_t)a; mem[nmem].data = (uint8_t)d;
        mem[nmem].pc = (uint16_t)pc; mem[nmem].used = 0;
        nmem++;
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    M6805 *c = &cpu;
    FILE *tf;
    char line[256];
    TraceLine prev, cur;
    long n = 0, ints = 0, cyc_err = 0;
    int have_prev = 0, step_due = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: trace6805 ROMDIR TRACE.txt [MAXERR]\n");
        return 2;
    }
    load(argv[1], "pz-0.113", rom, 0x800);
    load(argv[1], "bootstrap.bin", rom + 0x785, 0x73);
    if (argc > 3) maxerr = atol(argv[3]);
    load_memlog(argv[2]);
    tf = fopen(argv[2], "r");
    if (!tf) fail("cannot open %s\n", argv[2]);

    m6805_init(c);
    c->read = rd;
    c->write = wr;
    c->rom = rom;

    while (fgets(line, sizeof line, tf)) {
        cur_line++;
        if (have_prev && step_due && (strstr(line, "(interrupted at") || strstr(line, "T="))) {
            m6805_step(c);
            n++;
            step_due = 0;
        }
        if (have_prev && strstr(line, "(interrupted at")) {
            const char *q = strstr(line, ", ");
            if (q && !strncmp(q + 2, "IRQ 1", 5)) c->pending |= M6805_INT_TIMER;
            else { c->pending |= M6805_INT_IRQ; c->irq_pin = 1; }   /* host flag set: /INT low */
            m6805_step(c);
            c->pending = 0;
            ints++;
            continue;
        }
        if (!parse_line(line, &cur)) continue;
        if (!have_prev) {
            while (mpos < nmem && !(mem[mpos].kind == 'R' && mem[mpos].addr == cur.pc && mem[mpos].pc == cur.pc)) mpos++;
            c->a = cur.a; c->x = cur.x; c->s = cur.s; c->cc = cur.cc; c->pc = cur.pc;
            c->total = cur.t;
            prev = cur;
            have_prev = 1;
            step_due = 1;
            continue;
        }
        if (c->a != cur.a || c->x != cur.x || c->s != cur.s || c->cc != cur.cc || c->pc != cur.pc) {
            if (errors++ < maxerr)
                printf("line %ld after %03X %s:\n  core A=%02X X=%02X S=%02X CC=%02X PC=%03X\n  MAME A=%02X X=%02X S=%02X CC=%02X PC=%03X\n",
                       cur_line, prev.pc, prev.text, c->a, c->x, c->s, c->cc, c->pc, cur.a, cur.x, cur.s, cur.cc, cur.pc);
            c->a = cur.a; c->x = cur.x; c->s = cur.s; c->cc = cur.cc; c->pc = cur.pc;
        }
        if (c->total != cur.t) {
            if (cyc_err++ < maxerr)
                printf("line %ld after %03X %s: instruction %ld cycles, MAME %ld\n", cur_line, prev.pc, prev.text,
                       (long)(c->total - prev.t), (long)(cur.t - prev.t));
            c->total = cur.t;
        }
        prev = cur;
        step_due = 1;
    }
    fclose(tf);
    printf("%s: %ld instructions, %ld interrupts, %ld errors, %ld cycle errors\n", argv[2], n, ints, errors, cyc_err);
    return errors || cyc_err ? 1 : 0;
}
