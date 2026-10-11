/* YM2203 benchmark: replays a log of the YM2203 calls (xain_test with XAIN_YMLOG=FILE) on two
 * chips with synthesis on, prints the time and a checksum of the produced samples.
 *
 *   ymbench LOGFILE [FMDIV [DEBUG [REPEAT [START]]]]
 *
 * START: frames (52 224 input clocks each) replayed with the timers only (no synthesis, not
 * timed) before the measured part, e.g. to skip a silent start.
 * DEBUG as ym2203_debug: bit 0 = no SSG steps, bit 1 = no FM clock. REPEAT runs the log again
 * (from a reset) for longer measurements.
 */
#include <stdio.h>
#include <stdlib.h>
#include "ym2203.h"

extern unsigned long ym2203_ssg_iter, ym2203_ssg_runs;
#ifdef __VBCC__
#include <proto/dos.h>
#else
#include <time.h>
#endif

#define RING 4096

static YM2203 ym[2];
static int32_t ring[2][RING];

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

int main(int argc, char **argv)
{
    FILE *f;
    long size, n, i, t0, t1;
    uint32_t *log, sum = 0, pos[2] = { 0, 0 };
    unsigned char *raw;
    int fm_div = argc > 2 ? atoi(argv[2]) : 2, rep, repeat = argc > 4 ? atoi(argv[4]) : 1;
    long start_frames = argc > 5 ? atol(argv[5]) : 0, clocks_left, elapsed = 0;
    long samples = 0;
    if (argc < 2) {
        printf("usage: ymbench LOGFILE [FMDIV [DEBUG [REPEAT]]]\n");
        return 2;
    }
    if (!ym2203_layout_ok()) {
        printf("YM2203 struct layout differs from the assembler code\n");
        return 20;
    }
    f = fopen(argv[1], "rb");
    if (!f) {
        printf("cannot open %s\n", argv[1]);
        return 20;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    raw = malloc(size);
    log = malloc(size);
    if (!raw || !log || fread(raw, 1, size, f) != (size_t)size) return 20;
    fclose(f);
    n = size / 4;
    for (i = 0; i < n; i++)
        log[i] = (uint32_t)raw[4 * i] << 24 | (uint32_t)raw[4 * i + 1] << 16 | (uint32_t)raw[4 * i + 2] << 8 | raw[4 * i + 3];
    free(raw);
    ym2203_debug = argc > 3 ? atoi(argv[3]) : 0;

    for (rep = 0; rep < repeat; rep++) {
        for (i = 0; i < 2; i++) {
            ym2203_init(&ym[i]);
            ym[i].fm_div = (uint8_t)fm_div;
            ym[i].out = start_frames ? 0 : ring[i];
            ym[i].out_mask = RING - 1;
        }
        pos[0] = pos[1] = 0;
        clocks_left = start_frames * 52224L;
        t0 = now_ms();
        for (i = 0; i < n; i++) {
            uint32_t w = log[i];
            if (w & YM_TRACE_ADVANCE) {
                uint32_t k, count = (w >> 16) & 0x3fff;
                int32_t clocks = (int32_t)(w & 0xffff);
                for (k = 0; k < count; k++) {
                    ym2203_advance(&ym[0], clocks);
                    ym2203_advance(&ym[1], clocks);
                    if (clocks_left > 0 && (clocks_left -= clocks) <= 0) {
                        /* synthesis on from here (the time so far is not counted) */
                        ym[0].out = ring[0];
                        ym[1].out = ring[1];
                        pos[0] = ym[0].out_pos;
                        pos[1] = ym[1].out_pos;
                        t0 = now_ms();
                    }
                }
                /* fold the new samples into the checksum (the ring holds > 1 frame) */
                while (pos[0] != ym[0].out_pos) sum = sum * 31 + (uint32_t)ring[0][pos[0]++ & (RING - 1)];
                while (pos[1] != ym[1].out_pos) sum = sum * 31 + (uint32_t)ring[1][pos[1]++ & (RING - 1)];
            } else if (w & YM_TRACE_RESET) {
                ym2203_reset(&ym[(w >> 29) & 1]);
            } else {
                ym2203_write(&ym[(w >> 29) & 1], (int)(w >> 8) & 1, (uint8_t)w);
            }
        }
        samples += (long)pos[0];          /* out_pos only counts with synthesis on */
        elapsed += now_ms() - t0;
    }
    t1 = elapsed;
    t0 = 0;
    printf("%ld words, %ld FM output samples per chip, fmdiv %d, debug %d\n", n, samples, fm_div, ym2203_debug);
    printf("SSG: %lu runs, %lu iterations\n", ym2203_ssg_runs, ym2203_ssg_iter);
    {
        /* produced sound time: 41 667 FM samples per second (3 MHz / 72) */
        long sound_ms = samples * fm_div * 3 / 125;
        printf("time %ld ms for %ld ms of sound (%ld%% of real time), checksum %08lx\n", t1 - t0, sound_ms,
               sound_ms ? (t1 - t0) * 100 / sound_ms : 0L, (unsigned long)sum);
    }
    return 0;
}
