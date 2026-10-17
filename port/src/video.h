/* Xain'd Sleena video: graphics decoding and the reference renderer (line by line, as MAME's
 * partial updates). Output: 256x240 palette indices (0-511).
 */
#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>
#include "xain.h"

typedef struct Video {
    /* decoded graphics, one pen (0-15) per byte */
    uint8_t *chars;             /* 1024 x 8x8 */
    uint8_t *bg1;               /* 2048 x 16x16 (gfx2, RAM 2800) */
    uint8_t *bg2;               /* 2048 x 16x16 (gfx3, RAM 3000) */
    uint8_t *obj;               /* 2048 x 16x16 (gfx4) */
    uint16_t *fb;               /* the picture (fb_mem; the Amiga front end alternates two buffers) */
    uint16_t fb_mem[SCREEN_W * SCREEN_H];
    uint8_t rgb[512][3];
    /* fast renderer: per tile row 0 = all pens 0 (transparent), 1 = no pen 0 (covers), 2 = mixed */
    uint8_t *chars_rowtype;     /* 1024 x 8 */
    uint8_t *bg1_rowtype;       /* 2048 x 16 */
    uint8_t *bg2_rowtype;       /* 2048 x 16 */
    uint8_t *obj_rowtype;       /* 2048 x 16 */
    uint8_t char_empty[1024];   /* 1 = no visible pixel in the character */
} Video;

int video_init(Video *v, const Xain *x);
void video_free(Video *v);
/* renders scanline y (8..247) into fb; use as Xain.render_line with ctx = Video */
void video_render_line(void *ctx, Xain *x, int y);
void video_update_palette(Video *v, Xain *x);
/* fast renderer: the whole picture from the current state (the same as video_render_line for
   all lines 8..247 at this moment) */
void video_render_frame(Video *v, const Xain *x);
/* the same in parts: video_render_begin once per picture (sprite lists, tile lists), then
   video_render_lines for consecutive lines y0..y0+lines-1 (8..247, in order) into dst (SCREEN_W
   pixels per line); used by the Amiga front end to render in bands of 8 lines */
void video_render_begin(Video *v, const Xain *x);
void video_render_lines(Video *v, const Xain *x, int y0, int lines, uint16_t *dst);
/* Amiga: 1 = background and text lines in 68k assembler (default), 0 = C */
extern int video_asm;
extern int video_skip;            /* time measurements: bit 0 text, 1 BG1, 2 BG2, 3 sprites left out */

#endif
