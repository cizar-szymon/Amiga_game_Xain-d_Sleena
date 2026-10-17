/* Cost of the emulation of one frame from a machine state (F10 / build/probe/heavy.c), without
 * picture and sound, and how much of it the time slices cost: the same frames with slices of
 * 96 (normal: one per scanline), 48 and 32 cycles (2x and 3x as many slices, nearly the same
 * work of the CPUs), each fast path off, and per frame the counts of each kind of slice and the
 * cycles the 6809s really executed. Includes xain.c to reach its internals.
 *
 *   FrameCost ROMDIR STATEFILE [FRAMES]
 */
#include "../src/xain.c"
#include <stdio.h>
#include <stdlib.h>
#ifdef __VBCC__
#include <proto/dos.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/timer.h>
struct Device *TimerBase;
#else
#include <time.h>
#endif

/* the time of each part of a slice (xain_prof: -1 start, 0 main, 1 sub, 2 sound, 3 YM, 4 MCU);
   the rest of the frame (scanline(), main_fast_update(), the loop) is the frame minus these.
   prof_mode 1 = accumulate, 2 = only read the clock (its own cost, subtracted) */
static int prof_mode;
static uint32_t prof_last, prof_acc[5], prof_calls;
static uint32_t clock_lo(void)
{
#ifdef __VBCC__
    struct EClockVal ev;
    ReadEClock(&ev);
    return ev.ev_lo;
#else
    return (uint32_t)clock();
#endif
}
static void prof_hook(int part)
{
    uint32_t t = clock_lo();
    prof_calls++;
    if (prof_mode == 1 && part >= 0)
        prof_acc[part] += t - prof_last;
    prof_last = t;
}

static Xain mach;
static uint8_t *state;
static long state_len;

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

typedef struct {
    long us;                                /* per frame */
    uint32_t main_exec, sub_exec;           /* cycles executed (not skipped) per frame */
    uint32_t memo_hits, memo_runs, sub_fast;
} Result;

static void run(Result *r, long frames, int slice, int memo, int subfast)
{
    Xain *x = &mach;
    uint32_t mt, ms, st, ss, hits, runs, sf;
    long f, t;
    xain_load_state(x, state, (uint32_t)state_len);
    x->slice = slice;
    x->slice_memo = memo;
    x->sub_fast = subfast;
    x->in_p1 = x->in_p2 = 0;
    mt = x->main.total; ms = x->main.idle_skipped;
    st = x->sub.total; ss = x->sub.idle_skipped;
    hits = x->memo_hits; runs = x->memo_runs; sf = x->sub_fast_slices;
    t = now_ms();
    for (f = 0; f < frames; f++)
        xain_run_frame(x);
    t = now_ms() - t;
    r->us = t * 1000 / frames;
    r->main_exec = ((x->main.total - mt) - (x->main.idle_skipped - ms)) / (uint32_t)frames;
    r->sub_exec = ((x->sub.total - st) - (x->sub.idle_skipped - ss)) / (uint32_t)frames;
    r->memo_hits = (x->memo_hits - hits) / (uint32_t)frames;
    r->memo_runs = (x->memo_runs - runs) / (uint32_t)frames;
    r->sub_fast = (x->sub_fast_slices - sf) / (uint32_t)frames;
}

static void show(const char *name, const Result *r)
{
    printf("%-28s %6ld us/frame  exec main %5lu sub %5lu cyc  memo %3lu+%3lu  subfast %3lu\n", name, r->us,
           (unsigned long)r->main_exec, (unsigned long)r->sub_exec, (unsigned long)r->memo_hits,
           (unsigned long)r->memo_runs, (unsigned long)r->sub_fast);
}

int main(int argc, char **argv)
{
    const char *src[1];
    long frames = argc > 3 ? atol(argv[3]) : 600;
    Result r;
    FILE *fp;
    Xain *x = &mach;
    if (argc < 3) {
        printf("usage: FrameCost ROMDIR STATEFILE [FRAMES]\n");
        return 5;
    }
    src[0] = argv[1];
    if (xain_load_roms(x, src, 1)) return 10;
    xain_init(x);
#ifdef __VBCC__
    x->cpu_run = m6809_run_asm;
#endif
    x->sound_cpu = 0;
    xain_set_sound(x, 0);
    fp = fopen(argv[2], "rb");
    if (!fp) { printf("cannot open %s\n", argv[2]); return 10; }
    state = malloc(0x40000);
    state_len = (long)fread(state, 1, 0x40000, fp);
    fclose(fp);
    if (!xain_load_state(x, state, (uint32_t)state_len)) { printf("not a machine state\n"); return 10; }
    x->sound_cpu = 0;

    printf("%ld frames from %s, no picture, no sound\n", frames, argv[2]);
    run(&r, frames, 96, 1, 1); show("normal (slice 96)", &r);
    run(&r, frames, 48, 1, 1); show("slice 48 (2x slices)", &r);
    run(&r, frames, 32, 1, 1); show("slice 32 (3x slices)", &r);
    run(&r, frames, 96, 0, 1); show("MEMO off", &r);
    run(&r, frames, 96, 1, 0); show("SUBFAST off", &r);
    run(&r, frames, 96, 1, 1); show("normal again", &r);
#ifdef __VBCC__
    if (xain_translate(x)) {
        printf("translated: %lu instructions, %lu without template, %lu bytes\n", (unsigned long)m6809_tr_count,
               (unsigned long)m6809_tr_missing, (unsigned long)m6809_tr_bytes);
        printf("  specialized %lu, branches %lu, templates %lu, C core steps %lu\n",
               (unsigned long)m6809_tr_kinds[0], (unsigned long)m6809_tr_kinds[1],
               (unsigned long)m6809_tr_kinds[2], (unsigned long)m6809_tr_kinds[3]);
        run(&r, frames, 96, 1, 1); show("translated (TR)", &r);
        run(&r, frames, 96, 1, 1); show("translated again", &r);
        m6809_tr_free(&x->main);
    } else
        printf("translation not possible\n");
#endif
    {
        /* the parts: clock reads alone, then with accumulation; per frame in us */
        static const char *const part[5] = { "main CPU (run_main)", "sub CPU (run_sub)", "sound CPU",
                                             "YM2203", "MCU" };
        long base = r.us, cal, eclock_hz = 1000000L;
        uint32_t calls;
        int i;
#ifdef __VBCC__
        struct MsgPort *port = CreateMsgPort();
        struct timerequest *tr = (struct timerequest *)CreateIORequest(port, sizeof(struct timerequest));
        struct EClockVal ev;
        if (!tr || OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)tr, 0)) {
            printf("no timer.device\n");
            return 10;
        }
        TimerBase = tr->tr_node.io_Device;
        eclock_hz = (long)ReadEClock(&ev);
#else
        eclock_hz = CLOCKS_PER_SEC;
#endif
        xain_prof = prof_hook;
        prof_mode = 2; prof_calls = 0;
        run(&r, frames, 96, 1, 1);
        cal = r.us - base;                              /* clock reads per frame */
        calls = prof_calls / (uint32_t)frames;
        printf("clock reads: %lu per frame, %ld us per frame (subtracted below)\n", (unsigned long)calls, cal);
        prof_mode = 1; prof_calls = 0;
        memset(prof_acc, 0, sizeof prof_acc);
        run(&r, frames, 96, 1, 1);
        {
            long sum = 0, per_read10 = cal * 10 / (long)(calls ? calls : 1);   /* tenths of us */
            for (i = 0; i < 5; i++) {
                /* each part's time includes one clock read per slice */
                long us = (long)(prof_acc[i] / (uint32_t)frames) * 1000L / (eclock_hz / 1000L) -
                          per_read10 * (long)(calls / 6) / 10;
                if (us < 0) us = 0;
                sum += us;
                printf("  %-22s %6ld us per frame\n", part[i], us);
            }
            printf("  %-22s %6ld us per frame (scanline, 3A05, loop)\n", "rest", base - sum);
        }
        xain_prof = 0;
    }
    return 0;
}
