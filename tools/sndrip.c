/* sndrip: records every sound command of Xain'd Sleena separately (PC tool).
 *
 * Only the sound CPU and both YM2203 run (the main CPUs only write the sound latch, 3A08). For
 * each command: reset, WARMUP frames of silence, the command written to the latch as the main
 * CPU does it, then the sound is recorded until 3 s of silence or MAX seconds (music).
 *
 *   sndrip [-rate HZ] [-max SEC] [-cmd XX[,XX...]] OUTDIR
 *
 * Writes for every command that makes a sound:
 *   OUTDIR/cmd_XX.wav    16 bit mono, cut after the last sound (music: MAX seconds)
 *   OUTDIR/cmd_XX.state  the sound RAM 0000-01FF and the sample position (int32 little endian)
 *                        at each YM timer interrupt (the driver's tick, ca. 868/s) at which the
 *                        sequence pointers ($27-$3E) changed; tools/snd_loop.py finds the loops
 *   OUTDIR/sndrip.txt    command, samples, seconds, peak, 1 = still sounding at MAX
 * Build: sh tools/mksndrip.sh. Needs the ROMs in ROM/ or ROM/xsleena.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xain.h"
#include "romload.h"

#define WARMUP    60                  /* frames after reset before the command */
#define SILENCE   48                  /* |sample| below this is silence */

static Xain m;
static int16_t *rec;
static long nrec, maxrec;

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
    fwrite(b, 1, 4, f);
}

static void write_wav(const char *path, const int16_t *s, long n, int rate)
{
    FILE *f = fopen(path, "wb");
    long i;
    if (!f) { perror(path); exit(1); }
    fwrite("RIFF", 1, 4, f); put32(f, (uint32_t)(36 + n * 2)); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put32(f, 1 | 1 << 16); put32(f, (uint32_t)rate); put32(f, (uint32_t)rate * 2);
    put32(f, 2 | 16 << 16); fwrite("data", 1, 4, f); put32(f, (uint32_t)(n * 2));
    for (i = 0; i < n; i++) {
        uint8_t b[2];
        b[0] = (uint8_t)s[i]; b[1] = (uint8_t)((uint16_t)s[i] >> 8);
        fwrite(b, 1, 2, f);
    }
    fclose(f);
}

/* one scanline of the sound part only (as run_slice in xain.c with slice = 96) */
static void sound_line(int rate, int record)
{
    int16_t tmp[64];
    int n;
    m.cpu_run(&m.snd, XAIN_LINE_CYCLES);
    m.snd.firq_line = (uint8_t)ym2203_advance(&m.ym[0], XAIN_LINE_CYCLES * 2);
    ym2203_advance(&m.ym[1], XAIN_LINE_CYCLES * 2);
    while ((n = xain_take_audio(&m, tmp, 64, rate)) > 0)
        if (record) {
            if (nrec + n > maxrec) n = (int)(maxrec - nrec);
            memcpy(rec + nrec, tmp, (size_t)n * 2);
            nrec += n;
        }
}

int main(int argc, char **argv)
{
    const char *src[2] = { "ROM", "ROM/xsleena" };
    const char *outdir = 0;
    int rate = 15977, maxsec = 300, i;
    int cmds[256], ncmds = 0;
    FILE *rep;
    char path[512];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-rate") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-max") && i + 1 < argc) maxsec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-cmd") && i + 1 < argc) {
            char *p = argv[++i];
            while (*p && ncmds < 256) {
                cmds[ncmds++] = (int)strtol(p, &p, 16);
                if (*p == ',') p++;
            }
        } else outdir = argv[i];
    }
    if (!outdir) {
        fprintf(stderr, "usage: sndrip [-rate HZ] [-max SEC] [-cmd XX,..] OUTDIR\n");
        return 1;
    }
    if (!ncmds)
        for (i = 0; i < 256; i++) cmds[ncmds++] = i;
    if (xain_load_roms(&m, src, 2)) return 1;
    maxrec = (long)rate * maxsec;
    rec = malloc((size_t)maxrec * 2);
    sprintf(path, "%s/sndrip.txt", outdir);
    rep = fopen(path, "w");
    if (!rep || !rec) { perror(path); return 1; }
    fprintf(rep, "# cmd  samples  seconds  peak  at_max  (rate %d Hz)\n", rate);

    for (i = 0; i < ncmds; i++) {
        int cmd = cmds[i], peak = 0, last_firq = 0, line;
        long f, last_loud = -1, k;
        uint8_t last_ptr[0x18];
        FILE *st;
        xain_init(&m);
        nrec = 0;
        for (f = 0; f < WARMUP; f++)
            for (line = 0; line < XAIN_LINES; line++)
                sound_line(rate, 0);
        m.soundlatch = (uint8_t)cmd;                    /* as the main CPU: latch + IRQ */
        m.snd.irq_line = 1;
        sprintf(path, "%s/cmd_%02X.state", outdir, cmd);
        st = fopen(path, "wb");
        if (!st) { perror(path); return 1; }
        memset(last_ptr, 0xff, sizeof last_ptr);
        while (nrec < maxrec) {
            long before = nrec;
            for (line = 0; line < XAIN_LINES; line++) {
                sound_line(rate, 1);
                if (m.snd.firq_line && !last_firq && memcmp(last_ptr, m.snd_ram + 0x27, sizeof last_ptr)) {
                    memcpy(last_ptr, m.snd_ram + 0x27, sizeof last_ptr);
                    fwrite(m.snd_ram, 1, 0x200, st);
                    put32(st, (uint32_t)nrec);
                }
                last_firq = m.snd.firq_line;
            }
            for (k = before; k < nrec; k++) {
                int a = rec[k] < 0 ? -rec[k] : rec[k];
                if (a > peak) peak = a;
                if (a >= SILENCE) last_loud = k;
            }
            /* the end: 3 s of silence after the sound (or 3 s without any sound) */
            if (nrec - (last_loud < 0 ? 0 : last_loud) > 3L * rate) break;
        }
        fclose(st);
        if (last_loud < 0) {                            /* silent command */
            remove(path);
            continue;
        }
        if (nrec < maxrec) nrec = last_loud + 1;        /* one shot: cut the silence */
        sprintf(path, "%s/cmd_%02X.wav", outdir, cmd);
        write_wav(path, rec, nrec, rate);
        fprintf(rep, "%02X %8ld %7.2f %6d %d\n", cmd, nrec, (double)nrec / rate, peak, nrec >= maxrec);
        printf("%02X %7.2f s peak %5d%s\n", cmd, (double)nrec / rate, peak, nrec >= maxrec ? "  (max)" : "");
        fflush(stdout);
    }
    fclose(rep);
    return 0;
}
