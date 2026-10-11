/* Speed of the 6809 cores: a loop of typical code (copy with change, 16 bit arithmetic,
 * JSR/RTS, PSHS/PULS, branches) in 64 KB of flat RAM, run by the C core and (Amiga) by the
 * assembler core; prints emulated cycles and instructions per second and the speed against a
 * real 6809 at 1.5 MHz (Xain'd Sleena) and 1 MHz.
 *
 *   CpuBench [MEGACYCLES]
 *
 * Then micro benchmarks (asm core, Amiga): 32 equal instructions and a BRA back, per kind; the
 * result in 68060 cycles per 6809 instruction (CPU clock from the argument CLOCK_MHZ, default 50).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m6809.h"
#ifdef __VBCC__
#include <proto/dos.h>
#else
#include <time.h>
#endif

static M6809 cpu;
static uint8_t mem[0x10000];
static unsigned long instructions;

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

static const uint8_t program[] = {
    /* 8000 */ 0x8e, 0x10, 0x00,             /* start: LDX #$1000 */
    /* 8003 */ 0x10, 0x8e, 0x20, 0x00,       /*        LDY #$2000 */
    /* 8007 */ 0xc6, 0x40,                   /*        LDB #$40 */
    /* 8009 */ 0xa6, 0x80,                   /* loop:  LDA ,X+ */
    /* 800B */ 0x8b, 0x03,                   /*        ADDA #3 */
    /* 800D */ 0x84, 0x7f,                   /*        ANDA #$7F */
    /* 800F */ 0xa7, 0xa0,                   /*        STA ,Y+ */
    /* 8011 */ 0x5a,                         /*        DECB */
    /* 8012 */ 0x26, 0xf5,                   /*        BNE loop */
    /* 8014 */ 0xfc, 0x30, 0x00,             /*        LDD $3000 */
    /* 8017 */ 0xc3, 0x00, 0x01,             /*        ADDD #1 */
    /* 801A */ 0xfd, 0x30, 0x00,             /*        STD $3000 */
    /* 801D */ 0x86, 0x08,                   /*        LDA #8 */
    /* 801F */ 0xbd, 0x80, 0x24,             /* call:  JSR sub */
    /* 8022 */ 0x20, 0xdc,                   /*        BRA start */
    /* 8024 */ 0x34, 0x16,                   /* sub:   PSHS A,B,X */
    /* 8026 */ 0x96, 0x10,                   /*        LDA <$10 */
    /* 8028 */ 0x9b, 0x11,                   /*        ADDA <$11 */
    /* 802A */ 0x97, 0x12,                   /*        STA <$12 */
    /* 802C */ 0x1f, 0x89,                   /*        TFR A,B */
    /* 802E */ 0x81, 0x80,                   /*        CMPA #$80 */
    /* 8030 */ 0x24, 0x01,                   /*        BHS +1 */
    /* 8032 */ 0x4c,                         /*        INCA */
    /* 8033 */ 0x35, 0x96                    /*        PULS A,B,X,PC */
};

static void hook(M6809 *c) { (void)c; instructions++; }

static void setup(void)
{
    int p;
    memset(mem, 0x12, sizeof mem);
    memcpy(mem + 0x8000, program, sizeof program);
    mem[0xfffe] = 0x80;
    mem[0xffff] = 0x00;
    m6809_init(&cpu);
    for (p = 0; p < 256; p++)
        m6809_map_page(&cpu, p, mem + p * 256, mem + p * 256);
    m6809_reset(&cpu);
    cpu.s = 0x7f00;
    cpu.dp = 0x00;
}

static void bench(const char *name, int32_t (*run)(M6809 *, int32_t), long mcycles)
{
    long t0, t1, ms, k;
    unsigned long khz;
    setup();
    t0 = now_ms();
    for (k = 0; k < mcycles * 10; k++)
        run(&cpu, 100000);                    /* 0.1 M cycles per call */
    t1 = now_ms();
    ms = t1 - t0 > 0 ? t1 - t0 : 1;
    khz = (unsigned long)(mcycles * 1000000L / ms);     /* emulated cycles per ms = kHz */
    printf("%-4s %ld M cycles in %ld ms: %lu.%03lu MHz emulated = %lu.%lu x a 6809 at 1.5 MHz, %lu.%lu x at 1 MHz\n",
           name, mcycles, ms, khz / 1000, khz % 1000, khz * 10 / 1500 / 10, khz * 10 / 1500 % 10, khz / 1000,
           khz / 100 % 10);
}

/* micro benchmark loops: 32 x the instruction bytes, then BRA to the start */
typedef struct Micro {
    const char *name;
    uint8_t bytes[4];
    int len;
} Micro;

static const Micro micros[] = {
    { "NOP", { 0x12 }, 1 },
    { "INCA", { 0x4c }, 1 },
    { "ADDA #1", { 0x8b, 0x01 }, 2 },
    { "ANDA #$7F", { 0x84, 0x7f }, 2 },
    { "LDA $1000", { 0xb6, 0x10, 0x00 }, 3 },
    { "STA $1000", { 0xb7, 0x10, 0x00 }, 3 },
    { "LDA <$10", { 0x96, 0x10 }, 2 },
    { "LDA ,X", { 0xa6, 0x84 }, 2 },
    { "LDA ,X+", { 0xa6, 0x80 }, 2 },
    { "LDA 5,X", { 0xa6, 0x05 }, 2 },
    { "LDD $1000", { 0xfc, 0x10, 0x00 }, 3 },
    { "BRA +0", { 0x20, 0x00 }, 2 },
    { "BNE +0 (taken)", { 0x26, 0x00 }, 2 },
    { "TFR A,B", { 0x1f, 0x89 }, 2 },
};

static void micro(const Micro *mc, long mhz)
{
    int i, k, pc = 0x8000;
    long t0, t1, ms, n;
    unsigned long ins;
    setup();
    for (i = 0; i < 32; i++)
        for (k = 0; k < mc->len; k++)
            mem[pc++] = mc->bytes[k];
    mem[pc] = 0x20;                                     /* BRA start */
    mem[pc + 1] = (uint8_t)(0x8000 - (pc + 2));
    cpu.pc = 0x8000;
    cpu.x = 0x1000;
    cpu.cc = 0x50;                                      /* Z clear: BNE taken */
    /* instructions per run: count with the C core */
    instructions = 0;
    cpu.hook = hook;
    m6809_run(&cpu, 100000);
    cpu.hook = 0;
    ins = instructions;
    setup();
    for (i = 0, pc = 0x8000; i < 32; i++)
        for (k = 0; k < mc->len; k++)
            mem[pc++] = mc->bytes[k];
    mem[pc] = 0x20;
    mem[pc + 1] = (uint8_t)(0x8000 - (pc + 2));
    cpu.pc = 0x8000;
    cpu.x = 0x1000;
    cpu.cc = 0x50;
    t0 = now_ms();
    for (n = 0; n < 200; n++)
#ifdef __VBCC__
        m6809_run_asm(&cpu, 100000);
#else
        m6809_run(&cpu, 100000);
#endif
    t1 = now_ms();
    ms = t1 - t0 > 0 ? t1 - t0 : 1;
    /* 68060 cycles per instruction = mhz * 1000 * ms / (instructions in 200 runs) */
    printf("  %-16s %4lu ins per 100k cycles, %3ld.%ld 68060 cycles per instruction\n", mc->name, ins / 1000,
           mhz * 50L * ms / (long)ins / 10, mhz * 50L * ms / (long)ins % 10);   /* = mhz * 1e6 * ms / 1000 / (ins * 200) */
}

int main(int argc, char **argv)
{
    long mcycles = argc > 1 ? atol(argv[1]) : 50;
    long mhz = argc > 2 ? atol(argv[2]) : 50, ms;
    unsigned i;
    /* the instruction mix: count instructions of 1 M cycles with the C core */
    setup();
    cpu.hook = hook;
    m6809_run(&cpu, 1000000);
    cpu.hook = 0;
    printf("mix: %lu instructions per 1 M cycles (%lu.%02lu cycles per instruction)\n", instructions,
           1000000UL / instructions, 100000000UL / instructions % 100);
    bench("C", m6809_run, mcycles);
#ifdef __VBCC__
    if (!m6809_layout_ok()) {
        printf("layout differs\n");
        return 20;
    }
    bench("asm", m6809_run_asm, mcycles);
#endif
    printf("micro benchmarks (32 x the instruction + BRA; %s core, CPU at %ld MHz):\n",
#ifdef __VBCC__
           "asm",
#else
           "C",
#endif
           mhz);
    for (i = 0; i < sizeof micros / sizeof micros[0]; i++)
        micro(&micros[i], mhz);
    (void)ms;
    return 0;
}
