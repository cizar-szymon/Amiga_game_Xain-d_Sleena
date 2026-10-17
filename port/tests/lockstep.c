/* Lockstep test of the 6809 assembler core (Amiga): two machines with the same inputs, one with
 * the C core (m6809_run), one with the asm core (m6809_run_asm); after every frame all CPU
 * registers, cycle counters and memories must be equal.
 *
 *   Lockstep ROMDIR [FRAMES [FROM [STEP]]]
 *
 * From frame FROM on, the state after every CPU time slice (registers, cycles, the RAM of the
 * CPUs) is compared too; with STEP the slice that differs first is then run again instruction
 * by instruction in both machines (from the same state: the whole run is repeated) and the
 * first instruction with a different result is shown.
 *
 * Inputs: coin at 1201, start at 1301, fire at 1701 (planet), then running right, firing and
 * jumping (as the play1 scenario).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xain.h"
#include "romload.h"

static Xain ma, mb;

/* per slice comparison: machine A (C core) records, machine B (asm core) compares */
typedef struct Rec {
    uint16_t pc, x, y, u, s, ppc;
    uint8_t a, b, dp, cc;
    int32_t icount;
    uint32_t total, ram;
    int line;
} Rec;

#define MAXREC 60000
static Rec *rec;
static long nrec, ncmp;
static int tracing;              /* 1: record/compare the slices of the current frame */
static long step_slice = -1;     /* >= 0: run this slice (index in the frame) instruction by instruction */
static long found = -1;

static uint32_t hash(const uint8_t *p, int n, uint32_t h)
{
    int i;
    for (i = 0; i < n; i++) h = h * 31 + p[i];
    return h;
}

static void fill(Rec *r, const Xain *m, const M6809 *c)
{
    r->pc = c->pc; r->x = c->x; r->y = c->y; r->u = c->u; r->s = c->s;
    r->ppc = 0;                         /* not kept by the asm core (not used by the machine) */
    r->a = c->a; r->b = c->b; r->dp = c->dp; r->cc = c->cc;
    r->icount = c->icount;
    r->total = c->total;
    r->ram = hash(m->shared, sizeof m->shared, hash(m->snd_ram, sizeof m->snd_ram, 0));
    r->line = m->line;
}

static const char *cpu_name(const M6809 *c)
{
    return c == &ma.main || c == &mb.main ? "main" : c == &ma.sub || c == &mb.sub ? "sub" : "sound";
}

static void print_rec(const char *what, const Rec *r)
{
    printf("  %s A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X PC=%04X (ppc %04X) icount %ld total %lu ram %08lX\n",
           what, r->a, r->b, r->x, r->y, r->u, r->s, r->dp, r->cc, r->pc, r->ppc, (long)r->icount,
           (unsigned long)r->total, (unsigned long)r->ram);
}

/* one instruction at a time with the same budget as run(cpu, cycles) */
static int32_t run_steps(M6809 *c, int32_t cycles, int32_t (*run)(M6809 *, int32_t), Xain *m, int record)
{
    int32_t start;
    c->icount += cycles;
    start = c->icount;
    while (c->icount > 0) {
        int32_t ic = c->icount, used;
        Rec r;
        c->icount = 0;
        run(c, 1);
        used = 1 - c->icount;
        c->icount = ic - used;
        fill(&r, m, c);
        if (record) {
            if (nrec < MAXREC) rec[nrec++] = r;
        } else if (found < 0) {
            Rec *q = &rec[ncmp];
            if (ncmp >= nrec || memcmp(q, &r, sizeof r)) {
                printf("instruction %ld of the slice (%s): different\n", ncmp, cpu_name(c));
                if (ncmp > 0) print_rec("before", &rec[ncmp - 1]);
                if (ncmp < nrec) print_rec("C    ", q);
                print_rec("asm  ", &r);
                found = ncmp;
            }
            ncmp++;
        }
    }
    return start - c->icount;
}

static long slice_no;

/* the page tables of machine B against machine A (as offsets in each machine) and the ROMs */
static void check_maps(void)
{
    static const char *names[3] = { "main", "sub", "sound" };
    const M6809 *ca[3] = { &ma.main, &ma.sub, &ma.snd }, *cb[3] = { &mb.main, &mb.sub, &mb.snd };
    const char *ba = (const char *)&ma, *bb = (const char *)&mb;
    int k, p, bad = 0;
    for (k = 0; k < 3; k++)
        for (p = 0; p < 256; p++) {
            long ra = ca[k]->rpage[p] ? (long)((const char *)ca[k]->rpage[p] - ba) : -1;
            long rb = cb[k]->rpage[p] ? (long)((const char *)cb[k]->rpage[p] - bb) : -1;
            long qa = ca[k]->rbase[p] ? (long)((const char *)ca[k]->rbase[p] - ba) : -1;
            long qb = cb[k]->rbase[p] ? (long)((const char *)cb[k]->rbase[p] - bb) : -1;
            long wa = cb[k]->wbase[p] ? (long)((const char *)cb[k]->wbase[p] - bb) : -1;
            long xa = ca[k]->wbase[p] ? (long)((const char *)ca[k]->wbase[p] - ba) : -1;
            if (ra != rb || qa != qb || wa != xa) {
                if (bad++ < 20)
                    printf("%s page %02X: rpage C %lX asm %lX, rbase C %lX asm %lX, wbase C %lX asm %lX\n", names[k], p,
                           ra, rb, qa, qb, xa, wa);
            }
            if (cb[k]->rpage[p] && cb[k]->rbase[p] != cb[k]->rpage[p] - p * 256 && bad++ < 20)
                printf("%s page %02X (asm machine): rbase does not match rpage\n", names[k], p);
        }
    if (memcmp(ma.sub_rom, mb.sub_rom, sizeof ma.sub_rom) || memcmp(ma.sub_bank_rom, mb.sub_bank_rom, sizeof ma.sub_bank_rom) ||
        memcmp(ma.main_rom, mb.main_rom, sizeof ma.main_rom) || memcmp(ma.snd_rom, mb.snd_rom, sizeof ma.snd_rom))
        printf("ROM contents differ!\n");
    printf("page tables: %d differences\n", bad);
}

#ifdef LOCKSTEP_TRACE
/* the trace variant of the asm core (m6809_68k_trace.s): every dispatched instruction */
uint8_t m6809_trace_buf[1024 * 16];
uint32_t m6809_trace_pos;

static void print_trace(const M6809 *c)
{
    uint32_t i, n = 0;
    printf("asm trace of this CPU (the last instructions, oldest first):\n");
    for (i = m6809_trace_pos > 1024 ? m6809_trace_pos - 1024 : 0; i < m6809_trace_pos; i++) {
        const uint8_t *e = &m6809_trace_buf[(i & 1023) * 16];
        const M6809 *cpu = (const M6809 *)(uintptr_t)((uint32_t)e[12] << 24 | (uint32_t)e[13] << 16 |
                                                    (uint32_t)e[14] << 8 | e[15]);
        if (cpu != c) continue;
        if (++n > 400) break;
        printf("  PC=%04X A=%02X B=%02X X=%04X CC=%02X icount %ld\n", e[0] << 8 | e[1], e[2], e[3], e[4] << 8 | e[5],
               e[6], (long)(int32_t)((uint32_t)e[8] << 24 | (uint32_t)e[9] << 16 | (uint32_t)e[10] << 8 | e[11]));
    }
}
#endif

static int fast_until;            /* frames before FROM: machine A with the asm core too (faster) */

static int32_t run_c(M6809 *c, int32_t cycles)
{
    int32_t n;
    if (tracing && step_slice == slice_no) n = run_steps(c, cycles, m6809_run, &ma, 1);
    else if (fast_until) n = m6809_run_asm(c, cycles);
    else n = m6809_run(c, cycles);
    if (tracing && step_slice < 0 && nrec < MAXREC) fill(&rec[nrec++], &ma, c);
    slice_no++;
    return n;
}

static int32_t run_asm(M6809 *c, int32_t cycles)
{
    int32_t n;
    if (tracing && step_slice == slice_no) n = run_steps(c, cycles, m6809_run_asm, &mb, 0);
    else n = m6809_run_asm(c, cycles);
    if (tracing && step_slice < 0 && found < 0) {
        Rec r;
        fill(&r, &mb, c);
        if (ncmp >= nrec || memcmp(&rec[ncmp], &r, sizeof r)) {
            printf("CPU run %ld of the frame (%s, line %d): different\n", ncmp, cpu_name(c), r.line);
            if (ncmp >= 3) print_rec("C before", &rec[ncmp - 3]);
            if (ncmp < nrec) print_rec("C       ", &rec[ncmp]);
            print_rec("asm     ", &r);
#ifdef LOCKSTEP_TRACE
            print_trace(c);
#endif
            check_maps();
            found = ncmp;
        }
        ncmp++;
    }
    slice_no++;
    return n;
}

static int no_inputs;             /* STATE=: from a machine state, no inputs */

static void inputs(Xain *m, long f)
{
    m->in_p1 = m->in_p2 = 0;
    if (no_inputs) return;
    if (f >= 1201 && f < 1207) m->in_p2 = IN_P2_COIN1;
    if (f >= 1301 && f < 1307) m->in_p1 = IN_P1_START1;
    if (f >= 1701 && f < 1707) m->in_p1 = IN_BUTTON1;
    if (f >= 1801 && f < 4500)
        m->in_p1 = (uint8_t)(IN_RIGHT | ((f % 40) < 4 ? IN_BUTTON1 : 0) | ((f % 120) < 6 ? IN_BUTTON2 : 0));
}

static int cpu_diff(const char *name, const M6809 *a, const M6809 *b)
{
    if (a->a == b->a && a->b == b->b && a->x == b->x && a->y == b->y && a->u == b->u && a->s == b->s &&
        a->dp == b->dp && a->cc == b->cc && a->pc == b->pc && a->icount == b->icount && a->total == b->total &&
        a->nmi_pending == b->nmi_pending && a->waiting == b->waiting && a->lds_seen == b->lds_seen)
        return 0;
    printf("%s  C: A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X PC=%04X icount %ld total %lu\n",
           name, a->a, a->b, a->x, a->y, a->u, a->s, a->dp, a->cc, a->pc, (long)a->icount, (unsigned long)a->total);
    printf("%s asm: A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X PC=%04X icount %ld total %lu\n",
           name, b->a, b->b, b->x, b->y, b->u, b->s, b->dp, b->cc, b->pc, (long)b->icount, (unsigned long)b->total);
    return 1;
}

static int mem_diff(const char *name, const uint8_t *a, const uint8_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) {
            printf("%s differs at %04X: C %02X asm %02X\n", name, i, a[i], b[i]);
            return 1;
        }
    return 0;
}

int main(int argc, char **argv)
{
    const char *src[3];
    char path[512];
    long frames, f;
    int tr = 0, i, n;
    unsigned tr_to = 0xde89;
    const char *state_path = 0;
    /* keywords anywhere: TR (the asm machine with translated code), TRTO=hex (translate DCEC up
       to there only), STATE=file (both machines start from a machine state, no inputs) */
    for (i = n = 1; i < argc; i++) {
        if (!strcmp(argv[i], "TR")) tr = 1;
        else if (!strncmp(argv[i], "TRTO=", 5)) tr_to = (unsigned)strtoul(argv[i] + 5, 0, 16);
        else if (!strncmp(argv[i], "STATE=", 6)) state_path = argv[i] + 6;
        else argv[n++] = argv[i];
    }
    argc = n;
    setvbuf(stdout, NULL, _IONBF, 0);             /* each line on disk at once (if the machine hangs) */
    frames = argc > 2 ? atol(argv[2]) : 2000;
    long from = argc > 3 ? atol(argv[3]) : -1;
    int step = argc > 4;
    if (argc < 2) {
        printf("usage: Lockstep ROMDIR [FRAMES]\n");
        return 2;
    }
    if (!m6809_layout_ok()) {
        printf("M6809 struct layout differs from the assembler core\n");
        return 20;
    }
    src[0] = argv[1];
    path_join(path, sizeof path, argv[1], "xsleena");
    src[1] = path;
    if (xain_load_roms(&ma, src, 2) || xain_load_roms(&mb, src, 2)) return 20;
    xain_init(&ma);
    xain_init(&mb);
    rec = malloc(sizeof(Rec) * MAXREC);
    if (!rec) return 20;
again:
    ma.cpu_run = run_c;
    mb.cpu_run = run_asm;
    if (state_path) {
        static uint8_t sbuf[0x40000];
        FILE *fp = fopen(state_path, "rb");
        long len = fp ? (long)fread(sbuf, 1, sizeof sbuf, fp) : 0;
        if (fp) fclose(fp);
        if (!xain_load_state(&ma, sbuf, (uint32_t)len) || !xain_load_state(&mb, sbuf, (uint32_t)len)) {
            printf("%s: not a machine state\n", state_path);
            return 20;
        }
        no_inputs = 1;
    }
    if (tr && !(mb.main.trmap || m6809_tr_build(&mb.main, 0xdcec, (uint16_t)tr_to))) {
        printf("translation not possible\n");
        return 20;
    }
    if (tr) printf("translated: %lu instructions, %lu without template, %lu bytes\n", (unsigned long)m6809_tr_count,
                   (unsigned long)m6809_tr_missing, (unsigned long)m6809_tr_bytes);
    if (tr) printf("  specialized %lu, branches %lu, templates %lu, C core steps %lu\n",
                   (unsigned long)m6809_tr_kinds[0], (unsigned long)m6809_tr_kinds[1],
                   (unsigned long)m6809_tr_kinds[2], (unsigned long)m6809_tr_kinds[3]);
    xain_set_sound(&ma, 0);
    xain_set_sound(&mb, 0);
    for (f = 0; f < frames; f++) {
        int bad = 0;
        inputs(&ma, f);
        inputs(&mb, f);
        tracing = from >= 0 && f >= from;
        fast_until = from >= 0 && f < from;
        nrec = ncmp = 0;
        slice_no = 0;
        xain_run_frame(&ma);
        slice_no = 0;
        xain_run_frame(&mb);
        if (tracing && found >= 0) {
            printf("frame %ld\n", f);
            if (step && step_slice < 0) {
                /* again from the start, the slice found instruction by instruction */
                step_slice = found;
                found = -1;
                from = f;
                printf("stepping CPU run %ld of frame %ld\n", step_slice, f);
                xain_init(&ma);
                xain_init(&mb);
                goto again;
            }
            return 10;
        }
        bad |= cpu_diff("main", &ma.main, &mb.main);
        bad |= cpu_diff("sub", &ma.sub, &mb.sub);
        bad |= cpu_diff("sound", &ma.snd, &mb.snd);
        bad |= mem_diff("shared", ma.shared, mb.shared, sizeof ma.shared);
        bad |= mem_diff("charram", ma.charram, mb.charram, sizeof ma.charram);
        bad |= mem_diff("bgram0", ma.bgram0, mb.bgram0, sizeof ma.bgram0);
        bad |= mem_diff("bgram1", ma.bgram1, mb.bgram1, sizeof ma.bgram1);
        bad |= mem_diff("spriteram", ma.spriteram, mb.spriteram, sizeof ma.spriteram);
        bad |= mem_diff("palette", ma.palette, mb.palette, sizeof ma.palette);
        bad |= mem_diff("snd_ram", ma.snd_ram, mb.snd_ram, sizeof ma.snd_ram);
        bad |= mem_diff("mcu_ram", ma.mcu.ram, mb.mcu.ram, sizeof ma.mcu.ram);
        bad |= mem_diff("ym1", ma.ym[0].regs, mb.ym[0].regs, 256);
        bad |= mem_diff("ym2", ma.ym[1].regs, mb.ym[1].regs, 256);
        if (bad) {
            printf("frame %ld: DIFFERENT\n", f);
            return 10;
        }
        if (f % 100 == 0 || no_inputs)
            printf("frame %ld ok (main skipped %lu / %lu cycles)\n", f, (unsigned long)mb.main.idle_skipped,
                   (unsigned long)mb.main.total);
    }
    printf("%ld frames identical\n", frames);
    return 0;
}
