/* Xain'd Sleena (Technos 1986) machine model: 3x 6809, MC68705P5, 2x YM2203.
 * Follows MAME 0.289 xain.cpp (doc/mame0289). Platform independent C99.
 */
#ifndef XAIN_H
#define XAIN_H

#include <stdint.h>
#include "m6809.h"
#include "mcu68705.h"
#include "ym2203.h"

#define XAIN_LINES        272          /* scanlines per frame */
#define XAIN_LINE_CYCLES  96           /* 6809 cycles (1.5 MHz) per scanline: 384 pixels at 6 MHz */
#define XAIN_FRAME_CYCLES (XAIN_LINES * XAIN_LINE_CYCLES)
#define XAIN_FPS_X1000    57445        /* 6 MHz / 384 / 272 = 57.444853 Hz */
#define XAIN_FIRST_LINE   8            /* first visible scanline (MAME screen y) */
#define XAIN_YM_CLOCK     3000000      /* YM2203 clock; one FM sample every 72 clocks */
#define XAIN_YMBUF        4096         /* FM samples per ring (power of 2, > 2 frames) */
#define SCREEN_W          256
#define SCREEN_H          240

/* input bits (active high here; the machine inverts them) */
enum {
    IN_RIGHT = 0x01, IN_LEFT = 0x02, IN_UP = 0x04, IN_DOWN = 0x08,
    IN_BUTTON1 = 0x10, IN_BUTTON2 = 0x20,
    IN_P1_START1 = 0x40, IN_P1_START2 = 0x80,     /* in in_p1 */
    IN_P2_COIN1 = 0x40, IN_P2_COIN2 = 0x80        /* in in_p2 */
};

typedef struct Xain Xain;

/* a replayable time slice of the main CPU in a wait loop (run_main in xain.c) */
typedef struct {
    uint8_t used, a, cc, m3d, m3e, nmi;
    uint16_t pc;
    int32_t icount, cycles;
    uint8_t a2, cc2, nmi2;                /* the result */
    uint16_t pc2, ppc2;
    int32_t icount2;
    uint32_t dtotal, dskipped;
} XainSliceMemo;
#define XAIN_MEMO_SIZE 64

struct Xain {
    M6809 main, sub, snd;
    Mcu68705 mcu;
    YM2203 ym[2];

    /* ROMs */
    uint8_t main_rom[0x8000], main_bank_rom[2][0x4000];
    uint8_t sub_rom[0x8000], sub_bank_rom[2][0x4000];
    uint8_t snd_rom[0x8000];
    uint8_t mcu_rom[0x800], mcu_boot[0x73];
    uint8_t gfx_chars[0x8000];          /* gfx1 */
    uint8_t gfx_bg1[0x40000];           /* gfx2: BG1 tiles (RAM 2800) */
    uint8_t gfx_bg2[0x40000];           /* gfx3: BG2 tiles (RAM 3000) */
    uint8_t gfx_obj[0x40000];           /* gfx4: sprites */
    uint8_t prom[0x100];                /* priority (not used, as in MAME) */
    uint8_t zero_page[0x100];           /* reads of unmapped memory */

    /* RAM */
    uint8_t shared[0x2000];             /* 0000-1FFF main + sub */
    uint8_t charram[0x800];             /* 2000 text layer */
    uint8_t bgram1[0x800];              /* 2800 BG1 (MAME m_bgram[1]) */
    uint8_t bgram0[0x800];              /* 3000 BG2 (MAME m_bgram[0]) */
    uint8_t spriteram[0x200];           /* 3800-397F used (384 bytes) */
    uint8_t palette[0x200], palette_ext[0x200];
    uint8_t snd_ram[0x800];
    uint8_t ram_sink[0x100];            /* writes to ROM pages */

    /* registers */
    uint8_t scrollx[2][2], scrolly[2][2];   /* [0] = BG2 (3A04-07), [1] = BG1 (3A00-03) */
    uint8_t pri, main_bank, sub_bank, flip;
    uint8_t soundlatch;
    uint8_t vblank;
    uint8_t palette_dirty;              /* set on palette writes (for the renderer) */

    /* inputs and DIP switches */
    uint8_t in_p1, in_p2, coin3;
    uint8_t dsw0, dsw1;

    /* timing */
    int line;                           /* current scanline 0..271 */
    int slice;                          /* cycles per CPU time slice (divides 96) */
    int idle_skip;                      /* skip idle loops (exact; default on, set before reset) */
    int slice_memo;                     /* main CPU in its wait loops: replay whole time slices
                                           (exact; default on, see run_main in xain.c) */
    int sub_fast;                       /* sub CPU waiting in 8014: slices without the core
                                           (exact; default on, run_sub in xain.c) */
    int hle_sprites;                    /* main CPU in DCEC-DE88: the routine translated to C
                                           (exact; default off: vbcc's code is slower than
                                           the asm core on the 68060; m6809_run_spr) */
    uint32_t memo_hits, memo_runs;      /* statistics */
    uint32_t spr_slices, sub_fast_slices;

    /* front end help (xain_assist, START=EASY): coins inserted when not playing, joystick
       left / right = start 1P / 2P on the start screen */
    int autocoin, joystart;
    int coin_phase;
    XainSliceMemo memo[XAIN_MEMO_SIZE];
    int32_t (*cpu_run)(M6809 *cpu, int32_t cycles);   /* m6809_run (C) or m6809_run_asm (68k) */
    uint32_t frame;

    /* sound: both YM2203 write mixed samples at the FM rate (3 MHz / 72) into these rings */
    int32_t ymbuf[2][XAIN_YMBUF];
    uint32_t audio_pos;                 /* next FM sample to resample */
    uint32_t audio_frac;                /* resampler position between audio_pos and audio_pos+1 */
    int sound;                          /* 0 = timers only (no synthesis) */
    int sound_cpu;                      /* 0 = the sound CPU and the YM2203 not emulated at all (no
                                           sound; nothing else reads them, so the rest is exact) */
    /* every write to the sound latch (3A08), e.g. for sound from samples (sndplay.c); NULL = off */
    void (*sound_cmd)(void *ctx, uint8_t cmd);
    void *sound_ctx;

    /* renderer callback: draw scanline y (MAME screen y, 8..247) with the current state */
    void (*render_line)(void *ctx, Xain *x, int y);
    void *render_ctx;
};

typedef struct RomSet {
    const char *name, *parent, *title;
} RomSet;

void xain_init(Xain *x);
void xain_reset(Xain *x);
/* sources: zip files or directories (see romload.h); returns the number of missing ROMs */
int xain_load_roms(Xain *x, const char *const *sources, int nsources);
/* emulates one frame (272 scanlines from scanline 0) */
void xain_run_frame(Xain *x);
/* switch the sound synthesis on or off (off: only the YM2203 timers run) */
void xain_set_sound(Xain *x, int on);
/* START=EASY (not the original): call after the inputs of a frame are set. Inserts coins (through
   the coin input, as the coin slot) while no game runs and fewer than 2 credits are left, and on
   the start screen joystick left / right press start 1P / 2P. */
void xain_assist(Xain *x);
/* START=EASY: the start screen text "PUSH 1P OR 2P BUTTON" becomes "LEFT 1P OR RIGHT 2P" (same
   place, colour and font; ROM data in memory, only if the original bytes are there). 1 = done */
int xain_patch_texts(Xain *x);
/* FM output rate: 1 = every FM sample (exact, MAME), 2 = every second one (faster; phases and
   envelopes still run at the full rate) */
void xain_set_fm_div(Xain *x, int div);
/* resamples the sound produced since the last call to `rate` Hz (linear interpolation);
   returns the number of samples written (at most max) */
int xain_take_audio(Xain *x, int16_t *dst, int max, int rate);
/* profiling hook: called with -1 before a CPU time slice and 0..4 after the main CPU, the sub
   CPU, the sound CPU, both YM2203 and the MCU (NULL = off) */
extern void (*xain_prof)(int part);
/* RGB of a palette entry (4 bits per component expanded x17, as MAME) */
void xain_palette_rgb(const Xain *x, int index, uint8_t rgb[3]);

#endif
