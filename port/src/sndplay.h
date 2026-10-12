/* Sound from samples instead of the sound CPU and the YM2203 (docs/PORTING.md, chapter 13).
 *
 * The game's sound commands (writes to the sound latch 3A08) start prerecorded samples from
 * XainSleena.snd (tools/make_sndpack.py). One music voice and SFX_VOICES effect voices are mixed
 * into mono 16 bit samples. Rules as in the sound program: a music command that already plays
 * is ignored, FF stops the music, FE the effects, FD both, F0-FB one YM channel; an effect
 * replaces the effects that use one of its YM channels. Platform independent C99, no FPU.
 */
#ifndef SNDPLAY_H
#define SNDPLAY_H

#include <stdint.h>

#define SFX_VOICES 5
#define SND_MAX_CMDS 256

typedef struct {
    const int8_t *data;
    uint32_t len, loop;                 /* loop: restart offset (loops only) */
    uint16_t mask;                      /* YM channels of the sound */
    uint8_t vol, loops, present;
} SndEntry;

typedef struct {
    const SndEntry *e;
    uint32_t pos;                       /* sample position */
    uint32_t frac;                      /* 16 bit fraction (output rate != sample rate) */
    uint32_t age;
    uint8_t cmd, active;
} SndVoice;

typedef struct {
    SndEntry entry[SND_MAX_CMDS];
    SndVoice voice[1 + SFX_VOICES];     /* [0] = music */
    uint32_t rate;                      /* sample rate of the file */
    uint32_t frame_acc;                 /* fraction of a sample per frame */
    uint32_t clock;
    uint32_t ncmds;
} SndPlay;

/* pack: the whole file (kept by the caller); returns 0 or -1 (not an XSND file) */
int sndplay_init(SndPlay *p, const uint8_t *pack, uint32_t size);
/* stop everything (reset of the machine) */
void sndplay_reset(SndPlay *p);
/* a write to the sound latch */
void sndplay_command(SndPlay *p, uint8_t cmd);
/* mixes n samples at out_rate Hz into dst (overwrites) */
void sndplay_mix(SndPlay *p, int16_t *dst, int n, uint32_t out_rate);
/* the samples of one frame (57.445 Hz) at out_rate: returns how many were written (<= max) */
int sndplay_frame(SndPlay *p, int16_t *dst, int max, uint32_t out_rate);

#endif
