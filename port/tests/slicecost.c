/* Cost of the per scanline time slices (Amiga, asm 6809 core): the average frame, then the
 * cost of one slice of each part in a typical waiting state (repeated many times):
 *   sub CPU waiting in 8014 JMP 8014, the MCU in its wait loop, the main CPU in its wait loop
 *   (slice from the memo table and through the core), the scanline / 3A05 work, an empty core
 *   call. Includes xain.c to reach its internal functions.
 *
 *   SliceCost ROMDIR [FRAMES] [CALLS]
 */
#include "../src/xain.c"
#include <stdio.h>
#include <stdlib.h>
#ifdef __VBCC__
#include <proto/dos.h>
#else
#include <time.h>
#endif

static Xain mach;

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

static void report(const char *what, long ms, long calls)
{
    /* microseconds per call, as tenths; and per frame for 272 calls */
    long us10 = ms * 10000L / calls;                    /* ms <= ca. 100000: fits in 32 bits */
    printf("%-44s %4ld.%ld us per slice, %5ld us per frame (x272)\n", what, us10 / 10, us10 % 10,
           us10 * 272 / 10);
}

int main(int argc, char **argv)
{
    const char *src[1];
    long frames = argc > 2 ? atol(argv[2]) : 300, calls = argc > 3 ? atol(argv[3]) : 200000, i, t;
    Xain *x = &mach;
    if (argc < 2) {
        printf("usage: SliceCost ROMDIR [FRAMES] [CALLS]\n");
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
    {
        /* whole frames from the same start (frame 1000 of the demo), each fast path off / on */
        static const char *const name[4] = { "all fast paths", "MEMO off", "SUBFAST off", "none" };
        int v, play;
        for (play = 0; play < 2; play++)
            for (v = 0; v < 4; v++) {
                long f, start = play ? 1800 : 1000;
                xain_reset(x);
                x->slice_memo = !(v == 1 || v == 3);
                x->sub_fast = !(v == 2 || v == 3);
                t = 0;
                for (f = 0; f < start + frames; f++) {
                    uint8_t p1 = 0, p2 = 0;
                    if (play) {
                        /* coin, start, then walking, firing and jumping (as tools/memocheck.c) */
                        if (f >= 1201 && f < 1207) p2 = IN_P2_COIN1;
                        if (f >= 1301 && f < 1307) p1 = IN_P1_START1;
                        if (f >= 1801) {
                            int ph = (int)(f / 300) % 4;
                            p1 = (uint8_t)((ph == 1 ? IN_LEFT : IN_RIGHT) | (ph == 2 ? IN_UP : 0) |
                                           (ph == 3 ? IN_DOWN : 0) | ((f % 40) < 4 ? IN_BUTTON1 : 0) |
                                           ((f % 120) < 6 ? IN_BUTTON2 : 0));
                        }
                    }
                    x->in_p1 = p1;
                    x->in_p2 = p2;
                    if (f == start) t = now_ms();
                    xain_run_frame(x);
                }
                t = now_ms() - t;
                printf("whole frame (%s, no render), %-15s %ld us\n", play ? "play from 1800" : "demo from 1000",
                       name[v], t * 1000 / frames);
            }
        x->slice_memo = x->sub_fast = 1;
    }

    /* sub CPU waiting for its IRQ */
    x->sub.pc = 0x8014; x->sub.irq_line = 0; x->sub.firq_line = 0; x->sub.nmi_pending = 0; x->sub.icount = 0;
    t = now_ms();
    for (i = 0; i < calls; i++)
        x->cpu_run(&x->sub, XAIN_LINE_CYCLES);
    report("sub CPU, waiting (8014 JMP 8014), core", now_ms() - t, calls);
    x->sub.pc = 0x8014; x->sub.icount = 0;
    t = now_ms();
    for (i = 0; i < calls; i++)
        run_sub(x, XAIN_LINE_CYCLES);
    report("sub CPU, waiting, run_sub (SUBFAST)", now_ms() - t, calls);

    /* MCU in its wait loop */
    x->mcu.cpu.pc = 0x096; x->mcu.ram[0x3c] = x->mcu.ram[0x28] = 0;
    x->mcu.cpu.cc = (uint8_t)((x->mcu.cpu.cc & ~M6805_CC_N) | M6805_CC_Z); x->mcu.icount = 0;
    x->mcu.cpu.pending = 0; x->mcu.tcr &= (uint8_t)~0x80;    /* no timer interrupt waiting */
    x->mcu.cpu.ppc = 0xffff;
    t = now_ms();
    for (i = 0; i < calls; i++)
        mcu_run(&x->mcu, XAIN_LINE_CYCLES / 2);
    report("MCU, waiting (096-0A0)", now_ms() - t, calls);
    printf("  (MCU %s instructions during the test)\n", x->mcu.cpu.ppc == 0xffff ? "ran no" : "RAN");
    /* the same with the MCU's interrupts masked: only the pure skip (no timer interrupt) */
    x->mcu.cpu.pc = 0x096; x->mcu.cpu.pending = 0; x->mcu.tcr &= (uint8_t)~0x80; x->mcu.icount = 0;
    x->mcu.cpu.cc = (uint8_t)((x->mcu.cpu.cc & ~M6805_CC_N) | M6805_CC_Z | M6805_CC_I);
    x->mcu.cpu.ppc = 0xffff;
    t = now_ms();
    for (i = 0; i < calls; i++)
        mcu_run(&x->mcu, XAIN_LINE_CYCLES / 2);
    report("MCU, waiting, interrupts masked (pure skip)", now_ms() - t, calls);
    printf("  (MCU %s instructions during the test)\n", x->mcu.cpu.ppc == 0xffff ? "ran no" : "RAN");

    /* main CPU in its wait loop 8749: from the memo, then through the core */
    x->main.pc = 0x8749; x->main.dp = 0; x->shared[0x3d] = 1; x->shared[0x3e] = 0;
    x->main.nmi_pending = 0; x->main.firq_line = 0; x->main.irq_line = 0; x->main.icount = 0;
    x->slice_memo = 1;
    t = now_ms();
    for (i = 0; i < calls; i++)
        run_main(x, XAIN_LINE_CYCLES);
    report("main CPU, waiting, slice memo", now_ms() - t, calls);
    x->slice_memo = 0;
    t = now_ms();
    for (i = 0; i < calls; i++)
        run_main(x, XAIN_LINE_CYCLES);
    report("main CPU, waiting, core (MEMO=OFF)", now_ms() - t, calls);

    /* the per scanline work outside the CPUs */
    t = now_ms();
    for (i = 0; i < calls; i++)
        scanline(x, (int)(i % XAIN_LINES));
    report("scanline() (IRQ lines, VBLANK)", now_ms() - t, calls);
    t = now_ms();
    for (i = 0; i < calls; i++)
        main_fast_update(x);
    report("main_fast_update() (3A05 value)", now_ms() - t, calls);

    /* an empty core call: no cycles left */
    x->sub.icount = -1000000;
    t = now_ms();
    for (i = 0; i < calls; i++)
        x->cpu_run(&x->sub, 0);
    report("core call without cycles (entry/exit)", now_ms() - t, calls);
    return 0;
}
