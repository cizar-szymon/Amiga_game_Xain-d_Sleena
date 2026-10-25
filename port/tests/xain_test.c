/* Headless run of the machine for comparing builds (PC vs Amiga, C vs assembler cores):
 * the same inputs give the same screens, memory and sound on every platform.
 *
 *   xain_test ROMDIR OUTDIR FRAMES [SHOTLIST [SCRIPT]]
 *
 * Writes OUTDIR/frame_NNNNN.bmp and .bin (as xainsleena -shots/-dump) at the frames of
 * SHOTLIST ("100,200,1000-1100/10"), OUTDIR/sound.raw (16 bit big endian, 22050 Hz, mono)
 * and prints a checksum of every shot. SCRIPT: inputs as for xainsleena -script.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xain.h"
#include "video.h"
#include "romload.h"

#include <time.h>
#ifdef __VBCC__
#include <proto/dos.h>
#endif

/* milliseconds (Amiga: DateStamp, 20 ms steps; vbcc's clock() does not work) */
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
static Xain machine;
static Video video;

typedef struct { long frame; uint8_t *port; uint8_t bit; int press; } ScriptEvent;
static ScriptEvent script[2048];
static int script_count;
static uint8_t script_p1, script_p2;

static int input_bit(const char *name, uint8_t **port, uint8_t *bit)
{
    static const struct { const char *name; int p2; uint8_t bit; } names[] = {
        { "right", 0, IN_RIGHT }, { "left", 0, IN_LEFT }, { "up", 0, IN_UP }, { "down", 0, IN_DOWN },
        { "fire", 0, IN_BUTTON1 }, { "jump", 0, IN_BUTTON2 },
        { "start1", 0, IN_P1_START1 }, { "start2", 0, IN_P1_START2 },
        { "p2right", 1, IN_RIGHT }, { "p2left", 1, IN_LEFT }, { "p2up", 1, IN_UP }, { "p2down", 1, IN_DOWN },
        { "p2fire", 1, IN_BUTTON1 }, { "p2jump", 1, IN_BUTTON2 },
        { "coin1", 1, IN_P2_COIN1 }, { "coin2", 1, IN_P2_COIN2 }
    };
    int i;
    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (!strcmp(name, names[i].name)) {
            *port = names[i].p2 ? &script_p2 : &script_p1;
            *bit = names[i].bit;
            return 1;
        }
    return 0;
}

/* SCRIPT: "frame:+name,frame:-name,..." or @FILE with that text (long scripts on the Amiga) */
static void parse_script(const char *s)
{
    char *buf, *tok;
    if (s[0] == '@') {
        FILE *f = fopen(s + 1, "rb");
        long n;
        if (!f) {
            fprintf(stderr, "cannot open %s\n", s + 1);
            exit(2);
        }
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = malloc(n + 1);
        n = (long)fread(buf, 1, n, f);
        buf[n] = 0;
        fclose(f);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    } else {
        buf = malloc(strlen(s) + 1);
        strcpy(buf, s);
    }
    for (tok = strtok(buf, ","); tok && script_count < 2048; tok = strtok(0, ",")) {
        ScriptEvent *e = &script[script_count];
        char *colon = strchr(tok, ':');
        if (!colon || (colon[1] != '+' && colon[1] != '-') || !input_bit(colon + 2, &e->port, &e->bit)) {
            fprintf(stderr, "bad script entry: %s\n", tok);
            exit(2);
        }
        e->frame = atol(tok);
        e->press = colon[1] == '+';
        script_count++;
    }
    free(buf);
}

static void apply_script(long frame)
{
    int i;
    for (i = 0; i < script_count; i++)
        if (script[i].frame == frame) {
            if (script[i].press) *script[i].port |= script[i].bit;
            else *script[i].port &= (uint8_t)~script[i].bit;
        }
}

static int in_list(const char *list, long v)
{
    const char *p = list;
    while (*p) {
        long a = atol(p), b = a, step = 1;
        const char *q = p;
        while (*q && *q != ',') q++;
        {
            const char *dash = strchr(p, '-'), *slash = strchr(p, '/');
            if (dash && dash < q) b = atol(dash + 1);
            if (slash && slash < q) step = atol(slash + 1);
        }
        if (step < 1) step = 1;
        if (v >= a && v <= b && (v - a) % step == 0) return 1;
        p = *q ? q + 1 : q;
    }
    return 0;
}

static void put16(FILE *fp, unsigned v) { fputc((int)(v & 0xff), fp); fputc((int)((v >> 8) & 0xff), fp); }
static void put32(FILE *fp, unsigned long v) { put16(fp, (unsigned)(v & 0xffff)); put16(fp, (unsigned)(v >> 16)); }
static void put32be(FILE *fp, unsigned long v)
{
    fputc((int)(v >> 24), fp); fputc((int)((v >> 16) & 0xff), fp);
    fputc((int)((v >> 8) & 0xff), fp); fputc((int)(v & 0xff), fp);
}

static void save_bmp(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int x, y;
    unsigned long size = (unsigned long)SCREEN_W * 3 * SCREEN_H;
    if (!fp) { perror(path); return; }
    fputc('B', fp); fputc('M', fp);
    put32(fp, 54 + size); put32(fp, 0); put32(fp, 54);
    put32(fp, 40); put32(fp, SCREEN_W); put32(fp, SCREEN_H); put16(fp, 1); put16(fp, 24);
    put32(fp, 0); put32(fp, size); put32(fp, 2835); put32(fp, 2835); put32(fp, 0); put32(fp, 0);
    for (y = SCREEN_H - 1; y >= 0; y--)
        for (x = 0; x < SCREEN_W; x++) {
            const uint8_t *c = video.rgb[video.fb[y * SCREEN_W + x]];
            fputc(c[2], fp); fputc(c[1], fp); fputc(c[0], fp);
        }
    fclose(fp);
}

static void chunk(FILE *fp, const char *name, const uint8_t *data, unsigned long len)
{
    char n[16];
    memset(n, 0, sizeof n);
    memcpy(n, name, strlen(name) < sizeof n ? strlen(name) : sizeof n);
    fwrite(n, 1, 16, fp);
    put32be(fp, len);
    fwrite(data, 1, len, fp);
}

static void save_dump(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror(path); return; }
    fwrite("XSNP", 1, 4, fp);
    chunk(fp, "share1", machine.shared, 0x2000);
    chunk(fp, "charram", machine.charram, 0x800);
    chunk(fp, "bgram0", machine.bgram0, 0x800);
    chunk(fp, "bgram1", machine.bgram1, 0x800);
    chunk(fp, "spriteram", machine.spriteram, 0x180);
    chunk(fp, "palette", machine.palette, 0x200);
    chunk(fp, "palette_ext", machine.palette_ext, 0x200);
    chunk(fp, "sound_ram", machine.snd_ram, 0x800);
    chunk(fp, "mcu_ram", machine.mcu.ram, 0x80);
    chunk(fp, "ym1", machine.ym[0].regs, 256);
    chunk(fp, "ym2", machine.ym[1].regs, 256);
    fclose(fp);
}

static unsigned long checksum(void)
{
    unsigned long s = 0;
    int i;
    for (i = 0; i < SCREEN_W * SCREEN_H; i++)
        s = s * 31 + video.fb[i];
    for (i = 0; i < 0x2000; i++)
        s = s * 31 + machine.shared[i];
    return s & 0xffffffffUL;
}

/* XAIN_YMLOG=FILE: log of the YM2203 calls for tests/ymbench.c, big endian words:
   chip << 29 | write or reset word, or YM_TRACE_ADVANCE | count << 16 | clocks = count times
   (advance chip 0, advance chip 1) */
static FILE *ymlog;
static long ym_pend = -1, ym_run_clocks, ym_run_count;

static void ymlog_word(uint32_t w)
{
    fputc((int)(w >> 24), ymlog);
    fputc((int)(w >> 16) & 0xff, ymlog);
    fputc((int)(w >> 8) & 0xff, ymlog);
    fputc((int)w & 0xff, ymlog);
}

static void ymlog_flush(void)
{
    if (ym_run_count) ymlog_word(YM_TRACE_ADVANCE | (uint32_t)ym_run_count << 16 | (uint32_t)ym_run_clocks);
    ym_run_count = 0;
}

static void ymlog_trace(const YM2203 *ym, uint32_t w)
{
    int chip = ym == &machine.ym[1];
    if (w & YM_TRACE_ADVANCE) {
        long c = (long)(w & 0xffff);
        if (!chip) {
            ym_pend = c;
            return;
        }
        if (ym_pend != c) {
            fprintf(stderr, "ymlog: unpaired advance\n");
            exit(1);
        }
        ym_pend = -1;
        if (ym_run_count && ym_run_clocks == c && ym_run_count < 0x3fff) {
            ym_run_count++;
            return;
        }
        ymlog_flush();
        ym_run_clocks = c;
        ym_run_count = 1;
        return;
    }
    if (ym_pend >= 0) {
        fprintf(stderr, "ymlog: write between the advances\n");
        exit(1);
    }
    ymlog_flush();
    ymlog_word((uint32_t)chip << 29 | w);
}

int main(int argc, char **argv)
{
    static int16_t audio[2048];
    const char *src[3];
    char path[512], file[64];
    long frames, f;
    long t_start = 0;
    int no_render = 0;
    FILE *snd;
    if (argc < 4) {
        fprintf(stderr, "usage: xain_test ROMDIR OUTDIR FRAMES [SHOTLIST [SCRIPT]]\n");
        return 2;
    }
    src[0] = argv[1];
    path_join(path, sizeof path, argv[1], "xsleena");
    src[1] = path;
    src[2] = argv[1];
    if (xain_load_roms(&machine, src, 2)) return 1;
    frames = atol(argv[3]);
    if (getenv("XAIN_YMLOG")) {
        ymlog = fopen(getenv("XAIN_YMLOG"), "wb");
        if (!ymlog) return 1;
        ym2203_trace = ymlog_trace;
    }
    if (argc > 5) parse_script(argv[5]);
    xain_init(&machine);
    if (getenv("XAIN_NOIDLE")) {                       /* compare with idle loop skipping off */
        machine.idle_skip = 0;
        xain_reset(&machine);
    }
#ifdef __VBCC__
    if (getenv("XAIN_ASM"))                             /* the 6809 assembler core (Amiga) */
        machine.cpu_run = m6809_run_asm;
#endif
    if (getenv("XAIN_NOSNDCPU"))                       /* the sound CPU and YM2203 left out */
        machine.sound_cpu = 0;
    if (getenv("XAIN_YMDEBUG"))
        ym2203_debug = atoi(getenv("XAIN_YMDEBUG"));
    if (getenv("XAIN_FMDIV"))
        xain_set_fm_div(&machine, atoi(getenv("XAIN_FMDIV")));
    if (!video_init(&video, &machine)) return 1;
    machine.render_line = video_render_line;
    machine.render_ctx = &video;
    path_join(path, sizeof path, argv[2], "sound.raw");
    snd = fopen(path, "wb");
    apply_script(0);
    t_start = now_ms();
    if (getenv("XAIN_FAST"))                            /* the fast renderer at the end of each frame */
        machine.render_line = 0;
    if (getenv("XAIN_NORENDER")) {                      /* time measurements: no picture at all */
        machine.render_line = 0;
        no_render = 1;
    }
    for (f = 0; f < frames; f++) {
        int n, i;
        machine.in_p1 = script_p1;
        machine.in_p2 = script_p2;
        xain_run_frame(&machine);
        if (getenv("XAIN_CHECK")) {
            /* the fast renderer against the reference renderer drawing all lines now */
            static uint16_t ref[SCREEN_W * SCREEN_H];
            static long bad;
            int y;
            for (y = XAIN_FIRST_LINE; y < XAIN_FIRST_LINE + SCREEN_H; y++)
                video_render_line(&video, &machine, y);
            memcpy(ref, video.fb, sizeof ref);
            video_render_frame(&video, &machine);
            if (memcmp(ref, video.fb, sizeof ref) && bad++ < 10) {
                int k;
                for (k = 0; k < SCREEN_W * SCREEN_H && ref[k] == video.fb[k]; k++) ;
                printf("check-render: frame %ld differs at x %d y %d (%03x, fast %03x)\n", f, k % SCREEN_W,
                       k / SCREEN_W, ref[k], video.fb[k]);
            }
            if (f == frames - 1) printf("check-render: %ld of %ld frames differ\n", bad, frames);
            {
                /* the same in bands of 8 lines, each in spans of 1..8 blocks (video_render_span) */
                static long bad_span;
                static uint32_t seed = 1;
                int by, x0, k;
                memset(video.fb, 0xff, sizeof ref);
                video_render_begin(&video, &machine);
                for (by = 0; by < SCREEN_H / 8; by++)
                    for (x0 = 0; x0 < SCREEN_W;) {
                        int x1;
                        seed = seed * 1103515245u + 12345u;
                        x1 = x0 + 8 * (int)(1 + (seed >> 16) % 8);
                        if (x1 > SCREEN_W) x1 = SCREEN_W;
                        video_render_span(&video, &machine, XAIN_FIRST_LINE + by * 8, 8, x0, x1,
                                          video.fb + by * 8 * SCREEN_W);
                        x0 = x1;
                    }
                if (memcmp(ref, video.fb, sizeof ref) && bad_span++ < 10) {
                    for (k = 0; k < SCREEN_W * SCREEN_H && ref[k] == video.fb[k]; k++) ;
                    printf("check-span: frame %ld differs at x %d y %d (%03x, span %03x)\n", f, k % SCREEN_W,
                           k / SCREEN_W, ref[k], video.fb[k]);
                }
                if (f == frames - 1) printf("check-span: %ld of %ld frames differ\n", bad_span, frames);
            }
        } else if (!machine.render_line && !no_render) {
            video_render_frame(&video, &machine);
        }
        if (machine.palette_dirty) video_update_palette(&video, &machine);
        n = xain_take_audio(&machine, audio, 2048, 22050);
        for (i = 0; i < n && snd; i++) {
            fputc((audio[i] >> 8) & 0xff, snd);
            fputc(audio[i] & 0xff, snd);
        }
        if (argc > 4 && in_list(argv[4], f)) {
            snprintf(file, sizeof file, "frame_%05ld.bmp", f);
            path_join(path, sizeof path, argv[2], file);
            save_bmp(path);
            snprintf(file, sizeof file, "frame_%05ld.bin", f);
            path_join(path, sizeof path, argv[2], file);
            save_dump(path);
            printf("frame %ld: %08lx\n", f, checksum());
        }
        apply_script(f - 1);
    }
    printf("time: %ld ms for %ld frames\n", now_ms() - t_start, frames);
    if (snd) fclose(snd);
    if (ymlog) {
        ymlog_flush();
        fclose(ymlog);
    }
    printf("idle cycles skipped: main %lu, sub %lu (of %lu each)\n", (unsigned long)machine.main.idle_skipped,
           (unsigned long)machine.sub.idle_skipped, (unsigned long)machine.main.total);
    return 0;
}
