/* Renderer benchmark: runs the machine (no sound, Lockstep inputs) to the given frames and renders
 * each of them REPEAT times with the fast renderer; prints the time per frame and a checksum of
 * the picture (the same on the PC and the Amiga, and for every renderer variant).
 *
 *   RenderBench ROMDIR [REPEAT [FRAME ...]]
 *
 * Environment XAIN_SKIP=n: layers left out (video_skip, time measurements).
 */
#include <stdio.h>
#include <stdlib.h>
#include "xain.h"
#include "video.h"
#include "romload.h"
#ifdef __VBCC__
#include <proto/dos.h>
#else
#include <time.h>
#endif

static Xain m;
static Video v;

static long now_ms(void)
{
#ifdef __VBCC__
    struct DateStamp ds;
    DateStamp(&ds);
    return (ds.ds_Days * 1440L + ds.ds_Minute) * 60000L + ds.ds_Tick * 20L;
#else
    return (long)(clock() * 1000.0 / CLOCKS_PER_SEC);
#endif
}

static void inputs(Xain *x, long f)
{
    x->in_p1 = x->in_p2 = 0;
    if (f >= 1201 && f < 1207) x->in_p2 = IN_P2_COIN1;
    if (f >= 1301 && f < 1307) x->in_p1 = IN_P1_START1;
    if (f >= 1701 && f < 1707) x->in_p1 = IN_BUTTON1;
    if (f >= 1801 && f < 4500)
        x->in_p1 = (uint8_t)(IN_RIGHT | ((f % 40) < 4 ? IN_BUTTON1 : 0) | ((f % 120) < 6 ? IN_BUTTON2 : 0));
}

int main(int argc, char **argv)
{
    static const long def_frames[] = { 400, 900, 2000, 2400 };
    const char *src[2];
    char path[512];
    long repeat = argc > 2 ? atol(argv[2]) : 100, f = 0, total_ms = 0;
    int nf = argc > 3 ? argc - 3 : 4, k;
    if (argc < 2) {
        printf("usage: RenderBench ROMDIR [REPEAT [FRAME ...]]\n");
        return 2;
    }
    src[0] = argv[1];
    path_join(path, sizeof path, argv[1], "xsleena");
    src[1] = path;
    if (xain_load_roms(&m, src, 2)) return 20;
    xain_init(&m);
    xain_set_sound(&m, 0);
    if (!video_init(&v, &m)) return 20;
    if (getenv("XAIN_SKIP")) video_skip = atoi(getenv("XAIN_SKIP"));
    for (k = 0; k < nf; k++) {
        long target = argc > 3 ? atol(argv[3 + k]) : def_frames[k], r, t0, t1;
        unsigned long sum = 0;
        int i;
        while (f <= target) {
            inputs(&m, f);
            xain_run_frame(&m);
            f++;
        }
        {
            int variant, nv = video_asm ? 2 : 1;           /* Amiga: the asm lines, then C */
            for (variant = 0; variant < nv; variant++) {
                video_asm = variant == 0 && nv == 2;
                t0 = now_ms();
                for (r = 0; r < repeat; r++)
                    video_render_frame(&v, &m);
                t1 = now_ms();
                sum = 0;
                for (i = 0; i < SCREEN_W * SCREEN_H; i++)
                    sum = sum * 31 + v.fb[i];
                printf("frame %ld (pri %d) %s: %ld us per render, checksum %08lx\n", target, m.pri & 7,
                       nv == 2 && variant == 0 ? "asm" : "C", (t1 - t0) * 1000 / repeat, sum & 0xffffffffUL);
                if (variant == 0) total_ms += t1 - t0;
            }
            video_asm = nv == 2;
        }
    }
    printf("total %ld ms\n", total_ms);
    return 0;
}
