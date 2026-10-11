/* Yamaha YM2203 (OPN): timers, IRQ, FM (3 channels x 4 operators) and SSG synthesis.
 * C99 integer port of ymfm (MAME 0.289, BSD-3-Clause, Aaron Giles): doc/mame0289/ymfm_*.
 * Times are counted in input clocks (3 MHz). One FM sample every 72 clocks (prescale 6),
 * one SSG step every 16 clocks.
 */
#ifndef YM2203_H
#define YM2203_H

#include <stdint.h>

/* 32 bit fields first and sizes in multiples of 4: vbcc aligns longs only to 2 bytes, and
   misaligned longs are slow on the 68060 */
typedef struct YMOperator {
    uint32_t phase;             /* 10.10, after FM sample pt; later: + phase_step per sample when live */
    uint32_t pt;
    /* cache (cache_operator_data) */
    uint32_t phase_step;
    uint32_t total_level;
    uint32_t eg_sustain;
    uint32_t eg4;               /* envelope attenuation << 2 for the output, 0x1000 = silent */
    uint16_t env_att;           /* 4.6 attenuation, 0x3ff = silent */
    uint8_t eg_rate[4];
    uint8_t env_state;          /* 0 attack, 1 decay, 2 sustain, 3 release */
    uint8_t ssg_inverted;
    uint8_t key_state;
    uint8_t keyon_live;         /* bit 0 normal, bit 2 CSM */
    uint8_t opoffs;
    uint8_t ssg_en, ssg_mode;   /* register 90+: cached by prepare (all writes prepare first) */
    uint8_t live;               /* clocked (not released to silence without SSG-EG) */
    uint8_t pad[2];
} YMOperator;

typedef struct YMChannel {
    YMOperator *op[4];
    int16_t feedback[2];
    int16_t feedback_in;
    int16_t pad;
} YMChannel;

typedef struct YM2203 {
    uint8_t regs[256];          /* 00-0F SSG, 10-FF FM (B8-BF: frequency latches) */
    uint8_t addr;
    uint8_t status;             /* bit 0 timer A, bit 1 timer B */
    uint8_t irq;
    uint8_t prescale;           /* 6 (default), 3 or 2 */
    uint8_t timer_running[2];
    uint8_t modified;           /* FM registers written: prepare before the next FM sample */
    uint8_t active;             /* active channel mask (updated by prepare) */
    uint16_t ssg_ops;           /* operators with SSG-EG (clocked every sample) */
    uint16_t pad0;
    int32_t timer_left[2];      /* input clocks until expiry */
    uint32_t total_samples;     /* FM samples (ymfm m_total_clocks) */
    int32_t fm_phase;           /* input clocks into the current FM sample */
    int32_t ssg_phase;          /* input clocks into the current SSG step */

    /* FM engine */
    YMOperator op[12];
    YMChannel ch[3];
    uint32_t env_counter;
    uint32_t prepare_count;
    int32_t last_fm;            /* last FM output (after the DAC round trip) */

    /* SSG engine */
    uint32_t tone_count[3], tone_state[3];
    uint32_t env_count, env_state;
    uint32_t noise_count, noise_state;
    int32_t ssg_acc, ssg_n;     /* sum of SSG outputs (A+B+C) over the current FM sample */
    int32_t ssg_skip;           /* steps of a silent stretch not yet done on the counters */
    int32_t ssg_qcur;           /* output of the quiet SSG */
    uint32_t ssg_period[3], ssg_noise_period, ssg_env_period;
    int16_t ssg_amp[3];         /* fixed amplitude of each channel (non-envelope mode) */
    int16_t ssg_envamp;         /* amplitude of the envelope (envelope mode channels) */

    /* output: mixed samples at the FM rate, MAME xain levels: SSG x0.5, FM x0.4 (32768 = 1.0) */
    int32_t *out;               /* ring buffer (set by the owner), NULL = no synthesis */
    uint32_t out_mask;          /* size - 1 (power of 2) */
    uint32_t out_pos;           /* write position (free running) */

    uint8_t fm_div;             /* output every fm_div-th FM sample (1 = exact, 2 = half rate);
                                   phases and envelopes still run at the full rate */
    uint8_t div_count;
    uint8_t ssg_dirty;          /* SSG registers written: recompute the cached volumes */
    uint8_t ssg_quiet;          /* no SSG counter can change the output (until a register write) */
    uint8_t ssg_varied;         /* the SSG output may have changed in the current output sample */
    uint8_t ssg_rel_valid;      /* ssg_relevance: valid (until a write or an envelope step) */
    uint8_t ssg_tone_rel;       /* tone counters that can change the output (channel mask) */
    uint8_t ssg_noise_rel, ssg_env_rel;
    uint8_t pad[2];
} YM2203;

void ym2203_init(YM2203 *ym);
void ym2203_reset(YM2203 *ym);
/* offset 0 = address, 1 = data */
void ym2203_write(YM2203 *ym, int offset, uint8_t data);
/* advance by `clocks` input clocks (timers and sound); returns the IRQ state */
int ym2203_advance(YM2203 *ym, int32_t clocks);

/* call log for benchmarks (tests/ymbench.c): writes (offset << 8 | data), advances and resets */
#define YM_TRACE_ADVANCE 0x80000000u    /* | clocks */
#define YM_TRACE_RESET   0x40000000u
extern void (*ym2203_trace)(const YM2203 *ym, uint32_t word);
extern int ym2203_debug;                /* bit 0 = no SSG steps, bit 1 = no FM clock, bit 2 = FM output in C */
/* 1 if the struct layout is the one of the 68k FM output (Amiga) */
int ym2203_layout_ok(void);

#endif
