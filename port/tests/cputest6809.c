/* Random instruction test of the 6809 assembler core against the C core (Amiga).
 *
 *   CpuTest6809 [TESTS [SEED [CYCLES]]]
 *
 * Every test: random memory (64 KB, the same for both cores), random registers, a random
 * instruction (all opcodes of pages 0, 10 and 11 in turn, random operand bytes) at a random PC;
 * both cores run `CYCLES` cycles (default 1: one instruction; more: random code sequences).
 * Registers, cycle counters and the whole memory must be equal afterwards. With one instruction
 * pages 3A and C0-C1 go through the read/write handlers (the slow paths of the assembler core);
 * sequences use direct pages only (the assembler core fetches code from direct pages only, and
 * the game never runs code from I/O) and are not compared when the code runs over FFFF/0000
 * (the assembler core assumes that code does not run over the end of a memory region).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m6809.h"

static M6809 ca, cb;
static uint8_t base[0x10000], ma[0x10000], mb[0x10000];
static uint32_t rnd_state = 1;

static uint32_t rnd(void)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static int use_slow = 1;
static int near_wrap;            /* the C core ran code near FFFF/0000 (sequences: not compared) */

static void wrap_hook(M6809 *c)
{
    if (c->pc >= 0xff80 || c->pc < 0x0080) near_wrap = 1;
}
static int slow_page(int p) { return use_slow && (p == 0x3a || p == 0xc0 || p == 0xc1); }

static uint8_t rd(void *ctx, uint16_t a) { return ((uint8_t *)ctx)[a]; }
static void wr(void *ctx, uint16_t a, uint8_t v) { ((uint8_t *)ctx)[a] = v; }

static void setup(M6809 *c, uint8_t *m)
{
    int p;
    m6809_init(c);
    c->read = rd;
    c->write = wr;
    c->ctx = m;
    for (p = 0; p < 256; p++)
        if (slow_page(p)) m6809_map_page(c, p, 0, 0);
        else m6809_map_page(c, p, m + p * 256, m + p * 256);
}

static void print_state(const char *name, const M6809 *c)
{
    printf("  %s A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X PC=%04X icount %ld total %lu wait %d lds %d\n",
           name, c->a, c->b, c->x, c->y, c->u, c->s, c->dp, c->cc, c->pc, (long)c->icount, (unsigned long)c->total,
           c->waiting, c->lds_seen);
}

static int same(const M6809 *a, const M6809 *b)
{
    return a->a == b->a && a->b == b->b && a->x == b->x && a->y == b->y && a->u == b->u && a->s == b->s &&
           a->dp == b->dp && a->cc == b->cc && a->pc == b->pc && a->icount == b->icount && a->total == b->total &&
           a->waiting == b->waiting && a->lds_seen == b->lds_seen && a->nmi_pending == b->nmi_pending &&
           a->freerun == b->freerun;
}

int main(int argc, char **argv)
{
    long tests = argc > 1 ? atol(argv[1]) : 200000, t, fails = 0, skipped = 0;
    int32_t cycles = argc > 3 ? atol(argv[3]) : 1;
    static long fail_op[3][256];
    long i;
    if (argc > 2) rnd_state = (uint32_t)atol(argv[2]) | 1;
    use_slow = cycles <= 1;
    if (!m6809_layout_ok()) {
        printf("M6809 struct layout differs from the assembler core\n");
        return 20;
    }
    for (i = 0; i < 0x10000; i++) base[i] = (uint8_t)rnd();
    memcpy(ma, base, sizeof ma);
    memcpy(mb, base, sizeof mb);
    setup(&ca, ma);
    setup(&cb, mb);
    for (t = 0; t < tests; t++) {
        int page = (int)(t % 3), op = (int)((t / 3) & 0xff), k;
        uint16_t pc;
        uint8_t code[6];
        M6809 start;
        do pc = (uint16_t)rnd();
        while (slow_page(pc >> 8) || slow_page(((pc + 5) & 0xffff) >> 8));
        k = 0;
        if (page) code[k++] = (uint8_t)(page == 1 ? 0x10 : 0x11);
        code[k++] = (uint8_t)op;
        while (k < 6) code[k++] = (uint8_t)rnd();
        for (k = 0; k < 6; k++) ma[(pc + k) & 0xffff] = mb[(pc + k) & 0xffff] = code[k];

        ca.a = (uint8_t)rnd(); ca.b = (uint8_t)rnd();
        ca.x = (uint16_t)rnd(); ca.y = (uint16_t)rnd(); ca.u = (uint16_t)rnd(); ca.s = (uint16_t)rnd();
        ca.dp = (uint8_t)rnd(); ca.cc = (uint8_t)rnd(); ca.pc = pc;
        /* indexed modes often address near the registers: sometimes point them at slow pages */
        if ((rnd() & 7) == 0) ca.x = (uint16_t)(0x3a00 | (rnd() & 0xff));
        if ((rnd() & 7) == 0) ca.u = (uint16_t)(0xc0f0 | (rnd() & 0x1f));
        if ((rnd() & 7) == 0) ca.dp = 0x3a;
        ca.lds_seen = (uint8_t)(rnd() & 1);
        ca.waiting = 0;
        ca.freerun = 0;
        ca.nmi_pending = 0;
        ca.nmi_line = ca.firq_line = ca.irq_line = 0;
        ca.icount = 0;
        ca.total = rnd();
        cb.a = ca.a; cb.b = ca.b; cb.x = ca.x; cb.y = ca.y; cb.u = ca.u; cb.s = ca.s;
        cb.dp = ca.dp; cb.cc = ca.cc; cb.pc = ca.pc; cb.lds_seen = ca.lds_seen; cb.waiting = 0;
        cb.freerun = 0; cb.nmi_pending = 0; cb.nmi_line = cb.firq_line = cb.irq_line = 0;
        cb.icount = 0; cb.total = ca.total;
        start = ca;

        near_wrap = 0;
        ca.hook = cycles > 1 ? wrap_hook : 0;
        m6809_run(&ca, cycles);
        m6809_run_asm(&cb, cycles);

        if (near_wrap) {
            skipped++;
        } else if (!same(&ca, &cb) || memcmp(ma, mb, sizeof ma)) {
            fails++;
            fail_op[page][op]++;
            if (fails <= 25) {
                printf("test %ld: code %02X %02X %02X %02X %02X %02X\n", t, code[0], code[1], code[2], code[3],
                       code[4], code[5]);
                print_state("in ", &start);
                print_state("C  ", &ca);
                print_state("asm", &cb);
                for (i = 0; i < 0x10000; i++)
                    if (ma[i] != mb[i]) {
                        printf("  memory %04lX: C %02X asm %02X\n", i, ma[i], mb[i]);
                        break;
                    }
            }
        }
        /* restore the memory (only what can have changed is compared, but simply copy all) */
        memcpy(ma, base, sizeof ma);
        memcpy(mb, base, sizeof mb);
    }
    printf("%ld tests, %ld failures, %ld not compared (code near FFFF/0000)\n", tests, fails, skipped);
    if (fails) {
        int p, o;
        printf("failing opcodes:");
        for (p = 0; p < 3; p++)
            for (o = 0; o < 256; o++)
                if (fail_op[p][o]) printf(" %s%02X(%ld)", p == 0 ? "" : p == 1 ? "10" : "11", o, fail_op[p][o]);
        printf("\n");
    }
    return fails ? 10 : 0;
}
