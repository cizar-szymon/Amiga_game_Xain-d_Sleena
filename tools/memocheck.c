/* tools/memocheck.c: the main CPU fast paths (run_main in xain.c: slice memo, translated sprite
 * routine) against none: two machines, same
 * inputs (play1 as in the other probes), after every frame all RAM, the CPU registers and
 * cycle counters compared. Prints the first difference or OK, and the memo statistics. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xain.h"
#include "romload.h"

static Xain a, b;

static int same_cpu(const M6809 *p, const M6809 *q)
{
    return p->a == q->a && p->b == q->b && p->dp == q->dp && p->cc == q->cc && p->x == q->x &&
           p->y == q->y && p->u == q->u && p->s == q->s && p->pc == q->pc && p->icount == q->icount &&
           p->total == q->total && p->nmi_line == q->nmi_line && p->nmi_pending == q->nmi_pending &&
           p->firq_line == q->firq_line && p->irq_line == q->irq_line;
}

int main(int argc, char **argv)
{
    const char *src[2] = { "ROM", "ROM/xsleena" };
    long frames = argc > 1 ? atol(argv[1]) : 6000, f;
    int asm_free = 1;
    (void)asm_free;
    if (xain_load_roms(&a, src, 2) || xain_load_roms(&b, src, 2)) return 1;
    xain_init(&a);
    xain_init(&b);
    a.slice_memo = 0;
    a.hle_sprites = 0;
    b.hle_sprites = !getenv("ONLY_MEMO");                /* checked although off by default */
    if (getenv("ONLY_HLE")) b.slice_memo = 0;
    a.sound_cpu = b.sound_cpu = 0;
    xain_set_sound(&a, 0);
    xain_set_sound(&b, 0);
    for (f = 0; f < frames; f++) {
        uint8_t p1 = 0, p2 = 0;
        if (f >= 1201 && f < 1207) p2 = IN_P2_COIN1;
        if (f >= 1301 && f < 1307) p1 = IN_P1_START1;
        /* play: walk right, fire, jump, now and then left / up / down */
        if (f >= 1801) {
            int ph = (int)(f / 300) % 4;
            p1 = (uint8_t)((ph == 1 ? IN_LEFT : IN_RIGHT) | (ph == 2 ? IN_UP : 0) | (ph == 3 ? IN_DOWN : 0) |
                           ((f % 40) < 4 ? IN_BUTTON1 : 0) | ((f % 120) < 6 ? IN_BUTTON2 : 0));
        }
        a.in_p1 = b.in_p1 = p1;
        a.in_p2 = b.in_p2 = p2;
        xain_run_frame(&a);
        xain_run_frame(&b);
        if (memcmp(a.shared, b.shared, sizeof a.shared) || memcmp(a.charram, b.charram, sizeof a.charram) ||
            memcmp(a.bgram0, b.bgram0, sizeof a.bgram0) || memcmp(a.bgram1, b.bgram1, sizeof a.bgram1) ||
            memcmp(a.spriteram, b.spriteram, sizeof a.spriteram) || memcmp(a.palette, b.palette, sizeof a.palette) ||
            memcmp(a.palette_ext, b.palette_ext, sizeof a.palette_ext) || memcmp(a.scrollx, b.scrollx, sizeof a.scrollx) ||
            memcmp(a.scrolly, b.scrolly, sizeof a.scrolly) || a.soundlatch != b.soundlatch ||
            !same_cpu(&a.main, &b.main) || !same_cpu(&a.sub, &b.sub) ||
            memcmp(a.mcu.ram, b.mcu.ram, sizeof a.mcu.ram)) {
            printf("DIFFERENT after frame %ld (main pc %04X/%04X total %lu/%lu icount %ld/%ld)\n", f,
                   a.main.pc, b.main.pc, (unsigned long)a.main.total, (unsigned long)b.main.total,
                   (long)a.main.icount, (long)b.main.icount);
            return 1;
        }
    }
    printf("OK: %ld frames identical; memo: %lu replayed slices, %lu run (%.1f replayed per frame)\n", frames,
           (unsigned long)b.memo_hits, (unsigned long)b.memo_runs, (double)b.memo_hits / frames);
    printf("sprite routine translated: %.1f slices per frame\n", (double)b.spr_slices / frames);
    return 0;
}
