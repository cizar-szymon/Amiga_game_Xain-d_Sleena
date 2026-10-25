/* Xain'd Sleena reference renderer, see video.h. Follows MAME 0.289 xain_v.cpp and the gfx
 * layouts of xain.cpp (charlayout, tilelayout).
 */
#include "video.h"
#include <stdlib.h>
#include <string.h>

/* MAME bit numbering: bit n of a region is (src[n / 8] >> (7 - n % 8)) & 1 */
static int getbit(const uint8_t *src, uint32_t n)
{
    return (src[n >> 3] >> (7 - (n & 7))) & 1;
}

/* charlayout: 8x8, planes {0,2,4,6}, x {1,0,65,64,129,128,193,192}, y 0..56 step 8, 256 bits */
static void decode_chars(uint8_t *dst, const uint8_t *src, int count)
{
    static const uint16_t xo[8] = { 1, 0, 65, 64, 129, 128, 193, 192 };
    static const uint8_t po[4] = { 0, 2, 4, 6 };
    int c, x, y, p;
    for (c = 0; c < count; c++)
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++) {
                uint32_t base = (uint32_t)c * 256 + (uint32_t)y * 8 + xo[x];
                int pix = 0;
                for (p = 0; p < 4; p++)
                    pix = (pix << 1) | getbit(src, base + po[p]);   /* plane 0 = most significant */
                dst[(c * 8 + y) * 8 + x] = (uint8_t)pix;
            }
}

/* tilelayout: 16x16, planes {half+0, half+4, 0, 4}, x {3,2,1,0,131..128,259..256,387..384},
   y 0..120 step 8, 512 bits per tile, half = region size in bits / 2 */
static void decode_tiles(uint8_t *dst, const uint8_t *src, uint32_t region_bytes)
{
    uint32_t half = region_bytes * 8 / 2;
    uint32_t po[4];
    uint16_t xo[16];
    int count = (int)(half / 512), c, x, y, p;
    po[0] = half; po[1] = half + 4; po[2] = 0; po[3] = 4;
    for (x = 0; x < 16; x++)
        xo[x] = (uint16_t)((x >> 2) * 128 + 3 - (x & 3));
    for (c = 0; c < count; c++)
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                uint32_t base = (uint32_t)c * 512 + (uint32_t)y * 8 + xo[x];
                int pix = 0;
                for (p = 0; p < 4; p++)
                    pix = (pix << 1) | getbit(src, base + po[p]);
                dst[(c * 16 + y) * 16 + x] = (uint8_t)pix;
            }
}

/* 0 = all pens 0, 1 = no pen 0, 2 = mixed */
static void row_types(uint8_t *dst, const uint8_t *gfx, int rows, int width)
{
    int r, i;
    for (r = 0; r < rows; r++) {
        int zeros = 0, same = 1;
        for (i = 0; i < width; i++) {
            zeros += gfx[r * width + i] == 0;
            same &= gfx[r * width + i] == gfx[r * width];
        }
        /* 3: all pixels one pen, not 0 (the Amiga renderer fills such a row with long words) */
        dst[r] = (uint8_t)(zeros == width ? 0 : zeros == 0 ? (same ? 3 : 1) : 2);
    }
}

int video_init(Video *v, const Xain *x)
{
    memset(v, 0, sizeof(*v));
    v->fb = v->fb_mem;
    v->chars = malloc(1024 * 64);
    v->bg1 = malloc(2048 * 256);
    v->bg2 = malloc(2048 * 256);
    v->obj = malloc(2048 * 256);
    v->chars_rowtype = malloc(1024 * 8);
    v->bg1_rowtype = malloc(2048 * 16);
    v->bg2_rowtype = malloc(2048 * 16);
    v->obj_rowtype = malloc(2048 * 16);
    if (!v->chars || !v->bg1 || !v->bg2 || !v->obj || !v->chars_rowtype || !v->bg1_rowtype || !v->bg2_rowtype || !v->obj_rowtype)
        return 0;
    decode_chars(v->chars, x->gfx_chars, 1024);
    decode_tiles(v->bg1, x->gfx_bg1, 0x40000);
    decode_tiles(v->bg2, x->gfx_bg2, 0x40000);
    decode_tiles(v->obj, x->gfx_obj, 0x40000);
    row_types(v->chars_rowtype, v->chars, 1024 * 8, 8);
    row_types(v->bg1_rowtype, v->bg1, 2048 * 16, 16);
    row_types(v->bg2_rowtype, v->bg2, 2048 * 16, 16);
    row_types(v->obj_rowtype, v->obj, 2048 * 16, 16);
    {
        int c, r;
        for (c = 0; c < 1024; c++) {
            v->char_empty[c] = 1;
            for (r = 0; r < 8; r++)
                if (v->chars_rowtype[c * 8 + r]) v->char_empty[c] = 0;
        }
        for (c = 0; c < 2048; c++) {
            v->tile_empty[0][c] = v->tile_empty[1][c] = 1;
            for (r = 0; r < 16; r++) {
                if (v->bg2_rowtype[c * 16 + r]) v->tile_empty[0][c] = 0;
                if (v->bg1_rowtype[c * 16 + r]) v->tile_empty[1][c] = 0;
            }
        }
    }
    return 1;
}

void video_free(Video *v)
{
    free(v->bgcache[0]);
    free(v->bgcache[1]);
    v->bgcache[0] = v->bgcache[1] = 0;
    free(v->chars);
    free(v->bg1);
    free(v->bg2);
    free(v->obj);
    free(v->chars_rowtype);
    free(v->bg1_rowtype);
    free(v->bg2_rowtype);
    free(v->obj_rowtype);
    v->obj_rowtype = 0;
    v->chars = v->bg1 = v->bg2 = v->obj = 0;
    v->chars_rowtype = v->bg1_rowtype = v->bg2_rowtype = 0;
}

void video_update_palette(Video *v, Xain *x)
{
    int i;
    for (i = 0; i < 512; i++)
        xain_palette_rgb(x, i, v->rgb[i]);
    x->palette_dirty = 0;
}

/* ---- layers: write one line; opaque = draw pen 0 too ---- */

enum { L_TXT, L_BG1, L_BG2, L_SPR };

/* BG tilemaps: 32x32 tiles of 16x16, memory order from back_scan */
static void draw_bg(const Video *v, const Xain *x, int which, int y, uint16_t *line, int opaque)
{
    /* which: 0 = MAME m_bg_tilemaps[0] (RAM 3000, gfx3, colors 384), 1 = [1] (RAM 2800, gfx2, 256) */
    const uint8_t *ram = which == 0 ? x->bgram0 : x->bgram1;
    const uint8_t *gfx = which == 0 ? v->bg2 : v->bg1;
    int base = which == 0 ? 384 : 256;
    int sx = x->scrollx[which][0] | (x->scrollx[which][1] << 8);
    int sy = x->scrolly[which][0] | (x->scrolly[which][1] << 8);
    int ty = (y + sy) & 511, row = ty >> 4, fy = ty & 15, px;
    for (px = 0; px < SCREEN_W; px++) {
        int tx = (px + sx) & 511, col = tx >> 4;
        int idx = (col & 0x0f) + ((row & 0x0f) << 4) + ((col & 0x10) << 4) + ((row & 0x10) << 5);
        int attr = ram[idx | 0x400];
        int code = ram[idx] | ((attr & 7) << 8);
        int fx = tx & 15, pen;
        if (attr & 0x80) fx = 15 - fx;
        pen = gfx[(code * 16 + fy) * 16 + fx];
        if (pen || opaque)
            line[px] = (uint16_t)(base + ((attr & 0x70) >> 4) * 16 + pen);
    }
}

static void draw_txt(const Video *v, const Xain *x, int y, uint16_t *line, int opaque)
{
    int row = (y >> 3) & 31, fy = y & 7, px;
    for (px = 0; px < SCREEN_W; px++) {
        int idx = row * 32 + (px >> 3);
        int attr = x->charram[idx | 0x400];
        int code = x->charram[idx] | ((attr & 3) << 8);
        int pen = v->chars[(code * 8 + fy) * 8 + (px & 7)];
        if (pen || opaque)
            line[px] = (uint16_t)(((attr & 0xe0) >> 5) * 16 + pen);
    }
}

static void draw_tile_row(const Video *v, int code, int color, int flipx, int sx, int row, uint16_t *line)
{
    const uint8_t *src = v->obj + ((code & 0x7ff) * 16 + row) * 16;
    int i;
    for (i = 0; i < 16; i++) {
        int px = sx + i;
        int pen = src[flipx ? 15 - i : i];
        if (px >= 0 && px < SCREEN_W && pen)
            line[px] = (uint16_t)(128 + color * 16 + pen);
    }
}

static void draw_sprites(const Video *v, const Xain *x, int y, uint16_t *line)
{
    int offs;
    for (offs = 0; offs < 0x180; offs += 4) {
        int attr = x->spriteram[offs + 1];
        int code = x->spriteram[offs + 2] | ((attr & 7) << 8);
        int color = (attr & 0x38) >> 3;
        int flipx = (attr & 0x40) != 0;
        int sx = 238 - x->spriteram[offs + 3];
        int sy = 240 - x->spriteram[offs];
        if (sx <= -7) sx += 256;
        if (sy <= -7) sy += 256;
        if (attr & 0x80) {                              /* double height: code at sy-16, code+1 at sy */
            if (y >= sy - 16 && y < sy)
                draw_tile_row(v, code, color, flipx, sx, y - (sy - 16), line);
            if (y >= sy && y < sy + 16)
                draw_tile_row(v, code + 1, color, flipx, sx, y - sy, line);
        } else if (y >= sy && y < sy + 16) {
            draw_tile_row(v, code, color, flipx, sx, y - sy, line);
        }
    }
}

static void draw_layer(const Video *v, const Xain *x, int layer, int y, uint16_t *line, int opaque)
{
    switch (layer) {
    case L_TXT: draw_txt(v, x, y, line, opaque); break;
    case L_BG1: draw_bg(v, x, 1, y, line, opaque); break;
    case L_BG2: draw_bg(v, x, 0, y, line, opaque); break;
    default: draw_sprites(v, x, y, line); break;
    }
}

/* drawing order (bottom first) for the 8 priority modes, as in MAME screen_update */
static const uint8_t order[8][4] = {
    { L_BG2, L_BG1, L_SPR, L_TXT },
    { L_BG1, L_BG2, L_SPR, L_TXT },
    { L_TXT, L_BG2, L_SPR, L_BG1 },
    { L_TXT, L_BG1, L_SPR, L_BG2 },
    { L_BG2, L_TXT, L_SPR, L_BG1 },
    { L_BG1, L_TXT, L_SPR, L_BG2 },
    { L_BG2, L_SPR, L_BG1, L_TXT },
    { L_BG1, L_SPR, L_BG2, L_TXT },
};

void video_render_line(void *ctx, Xain *x, int y)
{
    Video *v = ctx;
    uint16_t *line = v->fb + (y - XAIN_FIRST_LINE) * SCREEN_W;
    const uint8_t *o = order[x->pri & 7];
    int i;
    for (i = 0; i < 4; i++)
        draw_layer(v, x, o[i], y, line, i == 0);
}

/* ------------------------------------------------------------------ fast renderer */

/*
 * The same picture as video_render_line for every line, composed line by line in the same
 * layer order, but with whole tile spans: the tile is looked up once per 8 or 16 pixels and
 * rows without any visible pixel are skipped (row types from video_init). Sprites: only the
 * ones on the screen, in RAM order. Compare with the reference: xainsleena -check-render.
 */

typedef struct SprEntry {
    int16_t sx, sy;             /* top left (screen y as MAME: 8..247 visible) */
    uint16_t cbase;             /* 128 + color * 16 */
    const uint8_t *gfx;         /* 16x16 pens of the tile */
    const uint8_t *rt;          /* its row types */
    uint8_t flipx;
} SprEntry;

/* the fast layers draw pixels x0..x1-1 of the line (0..SCREEN_W for the whole line) */
static void fast_bg(const Video *v, const Xain *x, int which, int y, uint16_t *line, int opaque, int x0, int x1)
{
    const uint8_t *ram = which == 0 ? x->bgram0 : x->bgram1;
    const uint8_t *gfx = which == 0 ? v->bg2 : v->bg1;
    const uint8_t *rt = which == 0 ? v->bg2_rowtype : v->bg1_rowtype;
    int base = which == 0 ? 384 : 256;
    int sx = (x->scrollx[which][0] | (x->scrollx[which][1] << 8)) & 511;
    int sy = x->scrolly[which][0] | (x->scrolly[which][1] << 8);
    int ty = (y + sy) & 511, row = ty >> 4, fy = ty & 15;
    int rowpart = ((row & 0x0f) << 4) + ((row & 0x10) << 5);
    int px = x0, tx = (sx + x0) & 511;
    while (px < x1) {
        int col = tx >> 4, fx0 = tx & 15, n = 16 - fx0, i;
        int idx = (col & 0x0f) + ((col & 0x10) << 4) + rowpart;
        int attr = ram[idx | 0x400];
        int code = ram[idx] | ((attr & 7) << 8);
        int kind = rt[code * 16 + fy];
        if (n > x1 - px) n = x1 - px;
        if (kind != 0 || opaque) {
            const uint8_t *src = gfx + (code * 16 + fy) * 16;
            uint16_t cb = (uint16_t)(base + ((attr & 0x70) >> 4) * 16);
            uint16_t *d = line + px;
            if (attr & 0x80) {
                const uint8_t *s = src + 15 - fx0;
                if (kind & 1 || opaque)
                    for (i = 0; i < n; i++) d[i] = (uint16_t)(cb | s[-i]);
                else
                    for (i = 0; i < n; i++) { int p = s[-i]; if (p) d[i] = (uint16_t)(cb | p); }
            } else {
                const uint8_t *s = src + fx0;
                if (kind & 1 || opaque)
                    for (i = 0; i < n; i++) d[i] = (uint16_t)(cb | s[i]);
                else
                    for (i = 0; i < n; i++) { int p = s[i]; if (p) d[i] = (uint16_t)(cb | p); }
            }
        }
        px += n;
        tx = (tx + n) & 511;
    }
}

/* x0, x1: multiples of 8 */
static void fast_txt(const Video *v, const Xain *x, int y, uint16_t *line, int opaque, int x0, int x1)
{
    int row = (y >> 3) & 31, fy = y & 7, col, i;
    const uint8_t *ram = x->charram + row * 32;
    for (col = x0 >> 3; col < x1 >> 3; col++) {
        int attr = ram[col | 0x400];
        int code = ram[col] | ((attr & 3) << 8);
        int kind = v->chars_rowtype[code * 8 + fy];
        if (kind != 0 || opaque) {
            const uint8_t *s = v->chars + (code * 8 + fy) * 8;
            uint16_t cb = (uint16_t)(((attr & 0xe0) >> 5) * 16);
            uint16_t *d = line + col * 8;
            if (kind & 1 || opaque)
                for (i = 0; i < 8; i++) d[i] = (uint16_t)(cb | s[i]);
            else
                for (i = 0; i < 8; i++) { int p = s[i]; if (p) d[i] = (uint16_t)(cb | p); }
        }
    }
}

/* the sprites of line y: list[0..n-1] (indices into spr, RAM order), pixels lx0..lx1-1 */
static void fast_sprites(const SprEntry *spr, const uint8_t *list, int n, int y, uint16_t *line, int lx0, int lx1)
{
    int k, i;
    for (k = 0; k < n; k++) {
        const SprEntry *e = &spr[list[k]];
        int r = y - e->sy, x0, x1;
        const uint8_t *s;
        if (!e->rt[r])
            continue;                                   /* no visible pixel in this row */
        s = e->gfx + r * 16;
        x0 = e->sx < lx0 ? lx0 - e->sx : 0;
        x1 = e->sx + 16 > lx1 ? lx1 - e->sx : 16;
        if (x0 >= x1)
            continue;
        if (e->rt[r] & 1) {                             /* no pen 0 (1, 3: one pen): all pixels */
            uint16_t *d = line + e->sx, cb = e->cbase;
            if (e->flipx)
                for (i = x0; i < x1; i++) d[i] = (uint16_t)(cb | s[15 - i]);
            else
                for (i = x0; i < x1; i++) d[i] = (uint16_t)(cb | s[i]);
        } else if (e->flipx) {
            for (i = x0; i < x1; i++) { int p = s[15 - i]; if (p) line[e->sx + i] = (uint16_t)(e->cbase | p); }
        } else {
            for (i = x0; i < x1; i++) { int p = s[i]; if (p) line[e->sx + i] = (uint16_t)(e->cbase | p); }
        }
    }
}

#ifdef __VBCC__
/*
 * The Amiga build: the lines of the backgrounds and the text in 68k assembler (video_68k.s,
 * generated by tools/gen_video_68k.py). The tiles are looked up once per tile row (16 lines of
 * a background, 8 of the text) into VTile lists; video_asm = 0 uses the C code (comparisons).
 */
typedef struct VTile {
    const uint8_t *pens;        /* pens of the tile */
    const uint8_t *rt;          /* row types of the tile */
    uint16_t cb;                /* palette base */
    uint16_t flip;              /* 0x80 = mirrored; text: byte offset of the cell in the line */
    uint32_t pad;
} VTile;

/* lines: that many consecutive lines (dst + 512 bytes each, fy + 1 each), one tile row / text row */
void video_bg_lines_68k(uint16_t *line, const VTile *t, uint32_t fx0, uint32_t fy, uint32_t opaque,
                        uint32_t first, uint32_t whole, uint32_t last, uint32_t lines);
void video_txt_lines_68k(uint16_t *line, const VTile *t, uint32_t n, uint32_t fy, uint32_t opaque, uint32_t lines);
int video_asm = 1;

/* two tile rows per background (a band of 8 lines can reach two; the Amiga draws a band in
   several spans), slot = row & 1 */
static VTile bg_tiles[2][2][17], txt_cells[32];
static int bg_row[2][2], txt_row, txt_n;

/* lines y0..y0+lines-1, pixels x0..x1-1 of a background into dst (the first of the lines): one
   call of the assembler per tile row */
static void lines_bg(const Video *v, const Xain *x, int which, int y0, int lines, uint16_t *dst, int opaque, int x0, int x1)
{
    const int sx = (x->scrollx[which][0] | (x->scrollx[which][1] << 8)) & 511;
    const int sy = x->scrolly[which][0] | (x->scrolly[which][1] << 8);
    /* the list starts with the tile at screen x 0; pixels x0..x1-1: the rest of the first tile,
       whole tiles, the start of the last one */
    const int off = (sx & 15) + x0, fo = off & 15, n = x1 - x0;
    const int first = fo ? (16 - fo < n ? 16 - fo : n) : 0;
    int y = y0;
    if (!video_asm) {
        for (; y < y0 + lines; y++)
            fast_bg(v, x, which, y, dst + (y - y0) * SCREEN_W, opaque, x0, x1);
        return;
    }
    while (y < y0 + lines) {
        const int ty = (y + sy) & 511, row = ty >> 4;
        int nl = 16 - (ty & 15);
        VTile *tl = bg_tiles[which][row & 1];
        if (nl > y0 + lines - y)
            nl = y0 + lines - y;
        if (row != bg_row[which][row & 1]) {
            const uint8_t *ram = which == 0 ? x->bgram0 : x->bgram1;
            const uint8_t *gfx = which == 0 ? v->bg2 : v->bg1;
            const uint8_t *rt = which == 0 ? v->bg2_rowtype : v->bg1_rowtype;
            int base = which == 0 ? 384 : 256, rowpart = ((row & 0x0f) << 4) + ((row & 0x10) << 5), k;
            for (k = 0; k < 17; k++) {
                int col = ((sx >> 4) + k) & 31;
                int idx = (col & 0x0f) + ((col & 0x10) << 4) + rowpart;
                int attr = ram[idx | 0x400];
                int code = ram[idx] | ((attr & 7) << 8);
                VTile *t = &tl[k];
                t->pens = gfx + code * 256;
                t->rt = rt + code * 16;
                t->cb = (uint16_t)(base + (attr & 0x70));
                t->flip = (uint16_t)(attr & 0x80);
            }
            bg_row[which][row & 1] = row;
        }
        video_bg_lines_68k(dst + (y - y0) * SCREEN_W + x0, &tl[off >> 4], (uint32_t)fo, (uint32_t)(ty & 15),
                           (uint32_t)opaque, (uint32_t)first, (uint32_t)((n - first) >> 4),
                           (uint32_t)((n - first) & 15), (uint32_t)nl);
        y += nl;
    }
}

/* the same for the text (x0, x1 multiples of 8): one call per text row */
static void lines_txt(const Video *v, const Xain *x, int y0, int lines, uint16_t *dst, int opaque, int x0, int x1)
{
    int y = y0;
    if (!video_asm) {
        for (; y < y0 + lines; y++)
            fast_txt(v, x, y, dst + (y - y0) * SCREEN_W, opaque, x0, x1);
        return;
    }
    while (y < y0 + lines) {
        const int row = (y >> 3) & 31;
        int nl = 8 - (y & 7), i0 = 0, i1;
        if (nl > y0 + lines - y)
            nl = y0 + lines - y;
        if (row != txt_row) {
            const uint8_t *ram = x->charram + row * 32;
            int col;
            txt_n = 0;
            for (col = 0; col < 32; col++) {
                int attr = ram[col | 0x400];
                int code = ram[col] | ((attr & 3) << 8);
                VTile *t;
                if (!opaque && v->char_empty[code])
                    continue;                           /* all 8 rows without a visible pixel */
                t = &txt_cells[txt_n++];
                t->pens = v->chars + code * 64;
                t->rt = v->chars_rowtype + code * 8;
                t->cb = (uint16_t)((attr & 0xe0) >> 1);
                t->flip = (uint16_t)(col * 16);
            }
            txt_row = row;
        }
        /* the cells are in x order: those from x0 to x1 */
        while (i0 < txt_n && txt_cells[i0].flip < x0 * 2)
            i0++;
        for (i1 = i0; i1 < txt_n && txt_cells[i1].flip < x1 * 2; i1++) ;
        if (i1 > i0)
            video_txt_lines_68k(dst + (y - y0) * SCREEN_W, &txt_cells[i0], (uint32_t)(i1 - i0), (uint32_t)(y & 7),
                                (uint32_t)opaque, (uint32_t)nl);
        y += nl;
    }
}
#else
int video_asm = 0;

static void lines_bg(const Video *v, const Xain *x, int which, int y0, int lines, uint16_t *dst, int opaque, int x0, int x1)
{
    int y;
    for (y = y0; y < y0 + lines; y++)
        fast_bg(v, x, which, y, dst + (y - y0) * SCREEN_W, opaque, x0, x1);
}

static void lines_txt(const Video *v, const Xain *x, int y0, int lines, uint16_t *dst, int opaque, int x0, int x1)
{
    int y;
    for (y = y0; y < y0 + lines; y++)
        fast_txt(v, x, y, dst + (y - y0) * SCREEN_W, opaque, x0, x1);
}
#endif

/* time measurements: layers left out (bit 0 text, 1 BG1, 2 BG2, 3 sprites) */
int video_skip;
int video_cache = 1;

/* ---- the bottom background as a 512x512 picture (bgcache) ---- */

/* tile idx (back_scan order) of background which into the cache */
static void cache_tile(Video *v, const Xain *x, int which, int idx)
{
    const uint8_t *ram = which == 0 ? x->bgram0 : x->bgram1;
    const uint8_t *gfx = which == 0 ? v->bg2 : v->bg1;
    const int col = (idx & 15) | ((idx >> 4) & 16), row = ((idx >> 4) & 15) | ((idx >> 5) & 16);
    const int attr = ram[idx | 0x400], code = ram[idx] | ((attr & 7) << 8);
    const uint16_t cb = (uint16_t)((which == 0 ? 384 : 256) + (attr & 0x70));
    const uint8_t *src = gfx + code * 256;
    uint16_t *dst = v->bgcache[which] + row * 16 * 512 + col * 16;
    int fy, fx;
    for (fy = 0; fy < 16; fy++, src += 16, dst += 512)
        if (attr & 0x80)
            for (fx = 0; fx < 16; fx++) dst[fx] = (uint16_t)(cb | src[15 - fx]);
        else
            for (fx = 0; fx < 16; fx++) dst[fx] = (uint16_t)(cb | src[fx]);
}

/* the cache of background which made equal to its tile RAM: everything the first time, then
   the tiles whose code or attribute changed */
static void cache_update(Video *v, const Xain *x, int which)
{
    const uint8_t *ram = which == 0 ? x->bgram0 : x->bgram1;
    uint8_t *old = v->cache_ram[which];
    int i;
    if (!v->bgcache[which]) {
        v->bgcache[which] = malloc(512 * 512 * sizeof(uint16_t));
        if (!v->bgcache[which]) {
            video_cache = 0;
            return;
        }
    }
    if (!v->cache_ok[which]) {
        for (i = 0; i < 0x400; i++)
            cache_tile(v, x, which, i);
        memcpy(old, ram, 0x800);
        v->cache_ok[which] = 1;
        return;
    }
    if (!memcmp(old, ram, 0x800))
        return;
    for (i = 0; i < 0x400; i++)
        if (old[i] != ram[i] || old[i | 0x400] != ram[i | 0x400])
            cache_tile(v, x, which, i);
    memcpy(old, ram, 0x800);
}

/* pixels x0..x1-1 of line y of background which from the cache (opaque) */
static void cache_line(const Video *v, const Xain *x, int which, int y, uint16_t *line, int x0, int x1)
{
    const int sx = (x->scrollx[which][0] | (x->scrollx[which][1] << 8)) & 511;
    const int sy = x->scrolly[which][0] | (x->scrolly[which][1] << 8);
    const uint16_t *src = v->bgcache[which] + ((y + sy) & 511) * 512;
    const int tx = (sx + x0) & 511, n = x1 - x0;
    if (tx + n <= 512)
        memcpy(line + x0, src + tx, (size_t)n * 2);
    else {
        memcpy(line + x0, src + tx, (size_t)(512 - tx) * 2);
        memcpy(line + x0 + 512 - tx, src, (size_t)(n - 512 + tx) * 2);
    }
}

/* the sprite lists of the picture being rendered (video_render_begin) */
static SprEntry spr[192];
static uint8_t list[SCREEN_H][192], count[SCREEN_H];

void video_render_begin(Video *v, const Xain *x)
{
    int n = 0, offs, y, i;

#ifdef __VBCC__
    bg_row[0][0] = bg_row[0][1] = bg_row[1][0] = bg_row[1][1] = txt_row = -1;   /* the tile lists are built again */
#endif
    /* the sprites that reach the visible lines, in RAM order (the top half of a double
       height sprite before its bottom half, as in MAME) */
    /* the bottom background: its cache up to date */
    if (video_cache) {
        const uint8_t *o = order[x->pri & 7];
        if (o[0] == L_BG2 || o[0] == L_BG1)
            cache_update(v, x, o[0] == L_BG2 ? 0 : 1);
    }
    for (offs = 0; offs < 0x180; offs += 4) {
        int attr = x->spriteram[offs + 1];
        int code = x->spriteram[offs + 2] | ((attr & 7) << 8);
        int sx = 238 - x->spriteram[offs + 3];
        int sy = 240 - x->spriteram[offs];
        int t;
        if (sx <= -7) sx += 256;
        if (sy <= -7) sy += 256;
        for (t = (attr & 0x80) ? 0 : 1; t < 2; t++) {
            int ty = t == 0 ? sy - 16 : sy;
            int c = t == 0 ? code : (attr & 0x80) ? code + 1 : code;
            if (ty + 16 <= XAIN_FIRST_LINE || ty >= XAIN_FIRST_LINE + SCREEN_H || sx >= SCREEN_W || sx <= -16)
                continue;
            spr[n].sx = (int16_t)sx;
            spr[n].sy = (int16_t)ty;
            spr[n].cbase = (uint16_t)(128 + ((attr & 0x38) >> 3) * 16);
            spr[n].gfx = v->obj + (c & 0x7ff) * 256;
            spr[n].rt = v->obj_rowtype + (c & 0x7ff) * 16;
            spr[n].flipx = (uint8_t)((attr & 0x40) != 0);
            n++;
        }
    }
    /* per line lists of the sprites (in RAM order) */
    memset(count, 0, sizeof count);
    for (i = 0; i < n; i++) {
        int y0 = spr[i].sy < XAIN_FIRST_LINE ? XAIN_FIRST_LINE : spr[i].sy;
        int y1 = spr[i].sy + 16 > XAIN_FIRST_LINE + SCREEN_H ? XAIN_FIRST_LINE + SCREEN_H : spr[i].sy + 16;
        for (y = y0; y < y1; y++) {
            int r = y - XAIN_FIRST_LINE;
            list[r][count[r]++] = (uint8_t)i;
        }
    }
}

/* 1 when background which has no visible pixel in lines y0..y0+lines-1, pixels x0..x1-1 (all its
   tiles there empty) */
static int span_empty_bg(const Video *v, const Xain *x, int which, int y0, int lines, int x0, int x1)
{
    const uint8_t *ram = which == 0 ? x->bgram0 : x->bgram1;
    const int sx = (x->scrollx[which][0] | (x->scrollx[which][1] << 8)) & 511;
    const int sy = x->scrolly[which][0] | (x->scrolly[which][1] << 8);
    const int r0 = ((y0 + sy) & 511) >> 4, nr = ((((y0 + sy) & 15) + lines - 1) >> 4) + 1;
    const int c0 = ((sx + x0) & 511) >> 4, nc = ((((sx + x0) & 15) + x1 - x0 - 1) >> 4) + 1;
    int r, c;
    for (r = 0; r < nr; r++) {
        const int row = (r0 + r) & 31, rowpart = ((row & 0x0f) << 4) + ((row & 0x10) << 5);
        for (c = 0; c < nc; c++) {
            const int col = (c0 + c) & 31, idx = (col & 0x0f) + ((col & 0x10) << 4) + rowpart;
            if (!v->tile_empty[which][ram[idx] | ((ram[idx | 0x400] & 7) << 8)])
                return 0;
        }
    }
    return 1;
}

/* the same for the text (x0, x1 multiples of 8) */
static int span_empty_txt(const Video *v, const Xain *x, int y0, int lines, int x0, int x1)
{
    int y, col;
    for (y = y0 & ~7; y < y0 + lines; y += 8) {
        const uint8_t *ram = x->charram + ((y >> 3) & 31) * 32;
        for (col = x0 >> 3; col < x1 >> 3; col++)
            if (!v->char_empty[ram[col] | ((ram[col | 0x400] & 3) << 8)])
                return 0;
    }
    return 1;
}

void video_render_span(Video *v, const Xain *x, int y0, int lines, int x0, int x1, uint16_t *dst)
{
    const uint8_t *o = order[x->pri & 7];
    /* the bottom background from its cache (video_render_begin made it up to date) */
    const int cached = video_cache && (o[0] == L_BG2 ? v->cache_ok[0] : o[0] == L_BG1 ? v->cache_ok[1] : 0);
    int y, i, skip = video_skip;
    /* the upper layers with nothing to draw in this span are left out for all its lines */
    for (i = 1; i < 4; i++)
        if ((o[i] == L_TXT && span_empty_txt(v, x, y0, lines, x0, x1)) ||
            (o[i] == L_BG1 && span_empty_bg(v, x, 1, y0, lines, x0, x1)) ||
            (o[i] == L_BG2 && span_empty_bg(v, x, 0, y0, lines, x0, x1)))
            skip |= 1 << o[i];
    /* layer by layer over all the lines (each layer covers the ones below pixel by pixel, so the
       order of the lines does not matter): one assembler call per layer and tile row */
    for (i = 0; i < 4; i++) {
        if (skip & (1 << o[i])) continue;
        switch (o[i]) {
        case L_TXT: lines_txt(v, x, y0, lines, dst, i == 0, x0, x1); break;
        case L_BG1:
        case L_BG2: {
            const int which = o[i] == L_BG2 ? 0 : 1;
            if (i == 0 && cached)
                for (y = y0; y < y0 + lines; y++)
                    cache_line(v, x, which, y, dst + (y - y0) * SCREEN_W, x0, x1);
            else
                lines_bg(v, x, which, y0, lines, dst, i == 0, x0, x1);
            break;
        }
        default:
            for (y = y0; y < y0 + lines; y++)
                fast_sprites(spr, list[y - XAIN_FIRST_LINE], count[y - XAIN_FIRST_LINE], y, dst + (y - y0) * SCREEN_W,
                             x0, x1);
            break;
        }
    }
}

void video_render_lines(Video *v, const Xain *x, int y0, int lines, uint16_t *dst)
{
    video_render_span(v, x, y0, lines, 0, SCREEN_W, dst);
}

void video_render_frame(Video *v, const Xain *x)
{
    video_render_begin(v, x);
    video_render_lines(v, x, XAIN_FIRST_LINE, SCREEN_H, v->fb);
}
