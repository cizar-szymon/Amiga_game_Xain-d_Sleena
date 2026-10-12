/* tools/spritecyc.c (build like tools/mksndrip.sh, run in the project folder > tools/sprites_cycles.txt):
 * the cycles of every instruction in DCEC-DE89 (main CPU) as the C core counts them,
 * per (address, next address); prints "pc next cycles count". Instructions followed by an
 * interrupt entry are left out (the next hook is at a vector). */
#include <stdio.h>
#include <stdlib.h>
#include "xain.h"
#include "romload.h"

static Xain m;
static uint32_t last_total;
static int last_pc = -1;
static unsigned cyc[0x200][0x10000 / 0x100];   /* not used */
static struct { uint16_t pc, next; uint8_t c; uint32_t n; } tab[4096];
static int ntab;

static void add(uint16_t pc, uint16_t next, uint32_t c)
{
    int i;
    for (i = 0; i < ntab; i++)
        if (tab[i].pc == pc && tab[i].next == next) {
            if (tab[i].c != c) printf("VARIES %04X -> %04X: %u and %u\n", pc, next, tab[i].c, (unsigned)c);
            tab[i].n++;
            return;
        }
    if (ntab < 4096) { tab[ntab].pc = pc; tab[ntab].next = next; tab[ntab].c = (uint8_t)c; tab[ntab].n = 1; ntab++; }
}

static void hook(M6809 *c)
{
    uint16_t pc = c->pc;
    if (last_pc >= 0xdcec && last_pc < 0xde89 && pc != 0x8053 && pc != 0x809b && pc != 0x80b9)
        add((uint16_t)last_pc, pc, c->total - last_total);
    last_pc = pc;
    last_total = c->total;
}

int main(void)
{
    const char *src[2] = { "ROM", "ROM/xsleena" };
    long f;
    int i;
    (void)cyc;
    if (xain_load_roms(&m, src, 2)) return 1;
    xain_init(&m);
    m.sound_cpu = 0;
    xain_set_sound(&m, 0);
    m.main.hook = hook;
    for (f = 0; f < 6000; f++) {
        m.in_p1 = m.in_p2 = 0;
        if (f >= 1201 && f < 1207) m.in_p2 = IN_P2_COIN1;
        if (f >= 1301 && f < 1307) m.in_p1 = IN_P1_START1;
        if (f >= 1801) {
            int ph = (int)(f / 300) % 4;
            m.in_p1 = (uint8_t)((ph == 1 ? IN_LEFT : IN_RIGHT) | (ph == 2 ? IN_UP : 0) | (ph == 3 ? IN_DOWN : 0) |
                                ((f % 40) < 4 ? IN_BUTTON1 : 0) | ((f % 120) < 6 ? IN_BUTTON2 : 0));
        }
        xain_run_frame(&m);
    }
    for (i = 0; i < ntab; i++)
        printf("%04X %04X %u %u\n", tab[i].pc, tab[i].next, tab[i].c, (unsigned)tab[i].n);
    return 0;
}
