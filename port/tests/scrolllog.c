/* Can the bottom layer be kept on the graphics card and scrolled by its blitter? For each frame
 * of an Amiga F10 replay (RAM:XainSleena-dump<n>.bin) at the end of the frame (as the Amiga
 * draws it): the bottom layer of the priority mode and its scroll step, the blocks of 8x8 where
 * the layers above it show (the picture differs from the bottom layer alone, now or in the last
 * frame), the blocks of the bottom layer whose colours changed (palette entries written with a
 * new value) and the new edge strip. A frame is "plate" when the bottom layer is a background
 * that scrolls by less than the screen; its cost is then the edge, the recoloured blocks and the
 * overlay blocks instead of all 960.
 *
 *   ScrollLog ROMDIR REPLAY [CSVFILE]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xain.h"
#include "video.h"
#include "romload.h"

#define BW (SCREEN_W / 8)
#define BH (SCREEN_H / 8)

static Xain m;
static Video v;
static uint16_t bottom[SCREEN_W * SCREEN_H], full[SCREEN_W * SCREEN_H], part[3][SCREEN_W * SCREEN_H], prev_full[SCREEN_W * SCREEN_H];
static uint8_t ovl[BH][BW], ovl_prev[BH][BW];

/* bottom layer of each priority mode, as video.c order[][0]: 0 text, 1 BG1, 2 BG2 */
static const int bottom_of[8] = { 2, 1, 0, 0, 2, 1, 2, 1 };
static const char *const layer_name[3] = { "text", "BG1", "BG2" };

static int scroll_of(const Xain *x, int layer, int y)
{
    int w = layer == 2 ? 0 : 1;                         /* scrollx[0] = BG2, [1] = BG1 */
    return y ? x->scrolly[w][0] | x->scrolly[w][1] << 8 : x->scrollx[w][0] | x->scrollx[w][1] << 8;
}

/* a signed step on the 512 ring */
static int step(int now, int old)
{
    int d = (now - old) & 511;
    return d >= 256 ? d - 512 : d;
}

int main(int argc, char **argv)
{
    static uint8_t rbuf[0x48000], pal_old[0x400];
    static uint8_t pal_chg[512];
    const char *src[2];
    char path[512];
    FILE *f, *csv = 0;
    size_t len;
    uint32_t start, count, slen, fr;
    int old_sx = 0, old_sy = 0, old_layer = -1, old_ox = 0, old_oy = 0;
    unsigned long n = 0, n_bgbottom = 0, n_scroll = 0, n_plate = 0, n_palbottom = 0, n_bigstep = 0;
    unsigned long sum_ovl = 0, sum_edge = 0, sum_recol = 0, sum_cost = 0, n_layer[3] = { 0, 0, 0 };
    unsigned long n_pal = 0, span_bad = 0;
    if (argc < 3) {
        printf("usage: ScrollLog ROMDIR REPLAY [CSVFILE]\n");
        return 2;
    }
    src[0] = argv[1];
    path_join(path, sizeof path, argv[1], "xsleena");
    src[1] = path;
    if (xain_load_roms(&m, src, 2)) return 20;
    xain_init(&m);
    xain_set_sound(&m, 0);
    f = fopen(argv[2], "rb");
    len = f ? fread(rbuf, 1, sizeof rbuf, f) : 0;
    if (f) fclose(f);
    start = (uint32_t)rbuf[4] << 24 | (uint32_t)rbuf[5] << 16 | (uint32_t)rbuf[6] << 8 | rbuf[7];
    count = (uint32_t)rbuf[8] << 24 | (uint32_t)rbuf[9] << 16 | (uint32_t)rbuf[10] << 8 | rbuf[11];
    slen = (uint32_t)rbuf[12] << 24 | (uint32_t)rbuf[13] << 16 | (uint32_t)rbuf[14] << 8 | rbuf[15];
    if (len < 16 || memcmp(rbuf, "XRP1", 4) || 16 + slen + count * 2 > len || !xain_load_state(&m, rbuf + 16, slen)) {
        printf("%s: not a replay\n", argv[2]);
        return 10;
    }
    xain_set_sound(&m, 0);
    m.sound_cpu = 0;
    if (!video_init(&v, &m)) return 20;
    if (argc > 3 && !(csv = fopen(argv[3], "w"))) return 10;
    if (csv) fprintf(csv, "frame,pri,bottom,dx,dy,pal_changed,pal_bottom_blocks,overlay_blocks,edge_blocks,plate_cost,txt_blocks,otherbg_blocks,spr_blocks,other_dx,other_dy,shift_blocks,still_blocks\n");
    memcpy(pal_old, m.palette, 0x200);
    memcpy(pal_old + 0x200, m.palette_ext, 0x200);
    memset(ovl_prev, 1, sizeof ovl_prev);
    for (fr = 0; fr < count; fr++) {
        const uint8_t *in = rbuf + 16 + slen + fr * 2;
        int pri, layer, sx, sy, dx = 0, dy = 0, bx, by, i, npal = 0;
        int n_ovl = 0, n_recol = 0, edge = 0, cost, plate, n_by[3] = { 0, 0, 0 }, odx, ody, n_shift, n_still;
        m.in_p1 = in[0];
        m.in_p2 = in[1];
        xain_run_frame(&m);
        pri = m.pri & 7;
        layer = bottom_of[pri];
        /* palette entries written with a new value since the last frame */
        for (i = 0; i < 512; i++) {
            pal_chg[i] = m.palette[i] != pal_old[i] || m.palette_ext[i] != pal_old[0x200 + i];
            npal += pal_chg[i];
        }
        memcpy(pal_old, m.palette, 0x200);
        memcpy(pal_old + 0x200, m.palette_ext, 0x200);
        /* the bottom layer alone, then the whole picture */
        video_skip = 15 & ~(1 << layer);
        video_render_frame(&v, &m);
        memcpy(bottom, v.fb, sizeof bottom);
        video_skip = 0;
        video_render_frame(&v, &m);
        memcpy(full, v.fb, sizeof full);
        {
            /* video_render_span in spans of 1..8 blocks gives the same picture */
            static uint32_t seed = 1;
            int x0;
            memset(v.fb, 0xff, sizeof full);
            video_render_begin(&v, &m);
            for (by = 0; by < BH; by++)
                for (x0 = 0; x0 < SCREEN_W;) {
                    int x1;
                    seed = seed * 1103515245u + 12345u;
                    x1 = x0 + 8 * (int)(1 + (seed >> 16) % 8);
                    if (x1 > SCREEN_W) x1 = SCREEN_W;
                    video_render_span(&v, &m, XAIN_FIRST_LINE + by * 8, 8, x0, x1, v.fb + by * 8 * SCREEN_W);
                    x0 = x1;
                }
            span_bad += memcmp(full, v.fb, sizeof full) != 0;
            /* and the whole picture as the reference renderer draws it */
            for (x0 = XAIN_FIRST_LINE; x0 < XAIN_FIRST_LINE + SCREEN_H; x0++)
                video_render_line(&v, &m, x0);
            span_bad += memcmp(full, v.fb, sizeof full) != 0;
        }
        /* the bottom layer with one more layer: text, BG1 (the other background), sprites */
        for (i = 0; i < 3; i++) {
            static const int other[3] = { 0, -1, 3 };
            int l = i == 1 ? (layer == 2 ? 1 : 2) : other[i];
            video_skip = 15 & ~(1 << layer) & ~(1 << l);
            video_render_frame(&v, &m);
            memcpy(part[i], v.fb, sizeof part[i]);
        }
        video_skip = 0;
        for (by = 0; by < BH; by++)
            for (bx = 0; bx < BW; bx++) {
                int y, x, o = 0, r = 0, lo[3] = { 0, 0, 0 };
                for (y = 0; y < 8; y++)
                    for (x = 0; x < 8; x++) {
                        int p = (by * 8 + y) * SCREEN_W + bx * 8 + x;
                        o |= bottom[p] != full[p];
                        r |= pal_chg[bottom[p]];
                        lo[0] |= bottom[p] != part[0][p];
                        lo[1] |= bottom[p] != part[1][p];
                        lo[2] |= bottom[p] != part[2][p];
                    }
                n_by[0] += lo[0];
                n_by[1] += lo[1];
                n_by[2] += lo[2];
                ovl[by][bx] = (uint8_t)o;
                n_ovl += o | ovl_prev[by][bx];          /* drawn now, or to be cleared */
                n_recol += r;
            }
        sx = layer ? scroll_of(&m, layer, 0) : 0;
        sy = layer ? scroll_of(&m, layer, 1) : 0;
        {   /* the step of the other background */
            int ol = layer == 2 ? 1 : 2, w = ol == 2 ? 0 : 1;
            int ox = m.scrollx[w][0] | m.scrollx[w][1] << 8, oy = m.scrolly[w][0] | m.scrolly[w][1] << 8;
            odx = step(ox, old_ox);
            ody = step(oy, old_oy);
            old_ox = ox;
            old_oy = oy;
        }
        if (layer == old_layer) {
            dx = step(sx, old_sx);
            dy = step(sy, old_sy);
        }
        /* the new strip in blocks: columns of 8 on the left or right, rows of 8 at top or bottom */
        if (dx) edge += ((abs(dx) + 7) / 8 + 1) * BH;
        if (dy) edge += ((abs(dy) + 7) / 8 + 1) * BW;
        plate = layer != 0 && layer == old_layer && abs(dx) < SCREEN_W && abs(dy) < SCREEN_H;
        cost = plate ? edge + n_recol + n_ovl : BW * BH;
        /* the whole last picture moved by the step (both backgrounds alike): the blocks that
           still differ from the new picture (new strip, sprites, text over the backgrounds, new
           tiles); and the blocks that differ without moving it */
        n_shift = n_still = 0;
        for (by = 0; by < BH; by++)
            for (bx = 0; bx < BW; bx++) {
                int y, x, ds = 0, dn = 0;
                for (y = 0; y < 8; y++)
                    for (x = 0; x < 8; x++) {
                        int px = bx * 8 + x, py = by * 8 + y, qx = px + dx, qy = py + dy;
                        uint16_t now = full[py * SCREEN_W + px];
                        dn |= now != prev_full[py * SCREEN_W + px];
                        ds |= qx < 0 || qx >= SCREEN_W || qy < 0 || qy >= SCREEN_H || now != prev_full[qy * SCREEN_W + qx];
                    }
                n_shift += ds;
                n_still += dn;
            }
        if (fr == 0) n_shift = n_still = BW * BH;
        memcpy(prev_full, full, sizeof full);
        if (cost > BW * BH) cost = BW * BH;
        n++;
        n_layer[layer]++;
        n_pal += npal != 0;
        if (layer) {
            n_bgbottom++;
            if (dx || dy) n_scroll++;
            if (abs(dx) >= 16 || abs(dy) >= 16) n_bigstep++;
        }
        if (plate) {
            n_plate++;
            sum_ovl += (unsigned long)n_ovl;
            sum_edge += (unsigned long)edge;
            sum_recol += (unsigned long)n_recol;
            if (n_recol) n_palbottom++;
        }
        sum_cost += (unsigned long)cost;
        if (csv)
            fprintf(csv, "%lu,%d,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", (unsigned long)(start + fr), pri,
                    layer_name[layer], dx, dy, npal, n_recol, n_ovl, edge, cost, n_by[0], n_by[1], n_by[2], odx, ody, n_shift, n_still);
        memcpy(ovl_prev, ovl, sizeof ovl);
        old_layer = layer;
        old_sx = sx;
        old_sy = sy;
    }
    printf("%s: %lu frames (Amiga frames %lu..%lu)\n", argv[2], n, (unsigned long)start, (unsigned long)(start + count - 1));
    printf("  bottom layer: text %lu, BG1 %lu, BG2 %lu frames; palette written with new values in %lu\n",
           n_layer[0], n_layer[1], n_layer[2], n_pal);
    printf("  background at the bottom: %lu frames, scrolled in %lu, steps >= 16 px in %lu\n", n_bgbottom, n_scroll, n_bigstep);
    if (n_plate)
        printf("  plate possible: %lu frames; per frame %lu.%lu overlay blocks, %lu.%lu edge blocks, "
               "%lu.%lu recoloured blocks (recolouring in %lu frames)\n",
               n_plate, sum_ovl / n_plate, sum_ovl * 10 / n_plate % 10, sum_edge / n_plate, sum_edge * 10 / n_plate % 10,
               sum_recol / n_plate, sum_recol * 10 / n_plate % 10, n_palbottom);
    printf("  video_render_span: %lu frames differ from the whole picture\n", span_bad);
    printf("  blocks composed per frame: %lu.%lu of %d with the plate (all %d without it)\n", sum_cost / n,
           sum_cost * 10 / n % 10, BW * BH, BW * BH);
    if (csv) fclose(csv);
    return 0;
}
