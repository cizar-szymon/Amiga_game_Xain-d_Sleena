#!/usr/bin/env python3
"""Generates port/src/video_68k.s: lines of the fast renderer in 68k assembler (the same pixels
as fast_bg / fast_txt in video.c; checked by tests/renderbench.c and XAIN_CHECK).

The tiles are looked up by C once per tile row (VTile lists, see video.c), the assembler only
draws one line from such a list:

  void video_bg_lines_68k(uint16_t *line, const VTile *t, uint32_t fx0, uint32_t fy, uint32_t opaque,
                          uint32_t first, uint32_t whole, uint32_t last, uint32_t lines)
      pixels fx0..fx0+first-1 of the first tile (none when first = 0), then `whole` whole tiles,
      then pixels 0..last-1 of the next one (a whole line: first = 16 - fx0 or 0, whole = 15 or
      16, last = fx0; a part of the line for the Amiga's blocks: any); for `lines` lines of the
      same tile row (line + 512 bytes, fy + 1 each; one call instead of one per line)
  void video_txt_lines_68k(uint16_t *line, const VTile *t, uint32_t n, uint32_t fy, uint32_t opaque,
                           uint32_t lines)
      n text cells (8 pixels at their x), for `lines` lines of the same text row

VTile (16 bytes): +0 pens of the tile (16x16 or 8x8 bytes), +4 its row types, +8 cb (palette
base, low 4 bits 0), +10 flip (bit 7; text: the byte offset of the cell in the line), +12 not
used. (Copying two pixels per long word from the pens as words was not faster on the A1200: the
time goes to cache misses on the tile graphics, twice as many with words.)
A pixel is cb | pen; rows of type 0 (all pens 0) are skipped unless opaque, rows of type 1 (no
pen 0) and opaque layers are copied, type 2 rows skip pen 0.
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "port", "src", "video_68k.s")
lines = []
label_no = [0]


def emit(*ls):
    lines.extend(ls)


def ins(s):
    lines.append("\t" + s)


def L():
    label_no[0] += 1
    return ".L%d" % label_no[0]


def copy_row(n, flip):
    """n pixels from (a5) (flip: backwards from a5 = end of the row) to (a0)+; cb in d1"""
    for _ in range(n):
        ins("move.w d1,d0")
        ins("or.b %s,d0" % ("-(a5)" if flip else "(a5)+"))
        ins("move.w d0,(a0)+")


def transp_row(n, flip):
    """n pixels, pen 0 transparent; a0 advanced by 2n at the end"""
    for k in range(n):
        skip = L()
        src = "%d(a5)" % ((n - 1 - k) if flip else k)
        ins("move.b %s,d2" % src)
        ins("beq.s %s" % skip)
        ins("move.w d1,d0")
        ins("or.b d2,d0")
        ins("move.w d0,%d(a0)" % (2 * k))
        emit(skip + ":")
    ins("lea %d(a0),a0" % (2 * n))


# registers: a0 line, a1 tile list, a4 opaque, d3 fy, d4 fy * width, d7 count
# per tile: a5 pens of the row, d1 cb, d6 row type

def tile_setup(width_shift):
    """a1 -> tile: a5 = pens of row fy, d1 = cb, d6 = row type (opaque: 1), flip in d5 (bit 7)"""
    ins("move.l (a1)+,a5")
    ins("move.l (a1)+,a6")
    ins("moveq #0,d6")
    ins("move.b (a6,d3.l),d6")       # row type
    ins("add.l d4,a5")               # row fy
    ins("move.w (a1)+,d1")           # cb
    ins("move.w (a1)+,d5")           # flip (text: x)
    ins("addq.l #4,a1")
    # opaque layer: a row without visible pixel is one pen (0) -> 3, a mixed row is copied -> 1
    nop, done = L(), L()
    ins("cmp.w #0,a4")
    ins("beq.s %s" % nop)
    ins("tst.b d6")
    ins("bne.s *+6")
    ins("moveq #3,d6")
    ins("bra.s %s" % done)
    ins("cmp.b #2,d6")
    ins("bne.s %s" % done)
    ins("moveq #1,d6")
    emit(nop + ":")
    emit(done + ":")


def whole_tile(width, done):
    """draw the whole row (width pixels) for the tile set up by tile_setup"""
    vis, copy, cflip, tflip, fill = L(), L(), L(), L(), L()
    ins("tst.b d6")
    ins("bne.s %s" % vis)
    ins("lea %d(a0),a0" % (2 * width))
    ins("bra %s" % done)
    emit(vis + ":")
    ins("cmp.b #3,d6")
    ins("bne.s %s" % fill)
    # one pen: cb | pen in both words of a long, width / 2 long word writes
    ins("move.w d1,d0")
    ins("or.b (a5),d0")
    ins("move.w d0,d2")
    ins("swap d0")
    ins("move.w d2,d0")
    for _ in range(width // 2):
        ins("move.l d0,(a0)+")
    ins("bra %s" % done)
    emit(fill + ":")
    ins("cmp.b #1,d6")
    ins("beq %s" % copy)
    if width == 16:
        ins("tst.b d5")
        ins("bmi %s" % tflip)
    transp_row(width, False)
    ins("bra %s" % done)
    if width == 16:
        emit(tflip + ":")
        transp_row(width, True)
        ins("bra %s" % done)
    emit(copy + ":")
    if width == 16:
        ins("tst.b d5")
        ins("bmi %s" % cflip)
    copy_row(width, False)
    ins("bra %s" % done)
    if width == 16:
        emit(cflip + ":")
        ins("lea 16(a5),a5")
        copy_row(width, True)
        ins("bra %s" % done)


def partial_tile():
    """pixels d0 .. d0 + d2 - 1 (d2 >= 1) of the 16 pixel tile set up by tile_setup"""
    vis, fwd, go, px, put, skip, done = L(), L(), L(), L(), L(), L(), L()
    ins("tst.b d6")
    ins("bne.s %s" % vis)
    ins("add.l d2,a0")
    ins("add.l d2,a0")
    ins("bra %s" % done)
    emit(vis + ":")
    ins("tst.b d5")
    ins("bpl.s %s" % fwd)
    ins("lea 15(a5),a5")
    ins("sub.l d0,a5")               # backwards from 15 - first
    ins("move.l #-1,a6")
    ins("bra.s %s" % go)
    emit(fwd + ":")
    ins("add.l d0,a5")
    ins("move.l #1,a6")
    emit(go + ":")
    ins("subq.l #1,d2")
    emit(px + ":")
    ins("move.b (a5),d5")
    ins("add.l a6,a5")
    ins("cmp.b #2,d6")                # 1 and 3: every pixel
    ins("bne.s %s" % put)
    ins("tst.b d5")
    ins("beq.s %s" % skip)
    emit(put + ":")
    ins("move.w d1,d0")
    ins("or.b d5,d0")
    ins("move.w d0,(a0)")
    emit(skip + ":")
    ins("addq.l #2,a0")
    ins("dbra d2,%s" % px)
    emit(done + ":")


def bg_line():
    # args after movem (11 registers, 44 bytes) and the return address: 48..
    emit("", "_video_bg_lines_68k:")
    ins("movem.l d2-d7/a2-a6,-(sp)")
    top = L()
    ins("move.l 80(sp),a3")          # lines
    emit(top + ":")
    ins("move.l 48(sp),a0")          # line
    ins("move.l 52(sp),a1")          # tiles
    ins("move.l 56(sp),d7")          # fx0
    ins("move.l 60(sp),d3")          # fy
    ins("move.l 64(sp),a4")          # opaque
    ins("move.l d3,d4")
    ins("lsl.l #4,d4")               # fy * 16
    first_done, mid, mid_done, mid_end, last_done = L(), L(), L(), L(), L()
    ins("tst.l 68(sp)")              # first
    ins("beq %s" % first_done)
    tile_setup(4)
    ins("move.l d7,d0")              # first pixel fx0
    ins("move.l 68(sp),d2")          # pixels
    partial_tile()
    emit(first_done + ":")
    ins("move.l 72(sp),d0")          # whole tiles
    ins("beq %s" % mid_end)
    ins("move.l d0,a2")
    emit(mid + ":")
    tile_setup(4)
    whole_tile(16, mid_done)
    emit(mid_done + ":")
    ins("subq.l #1,a2")
    ins("cmp.w #0,a2")
    ins("bne %s" % mid)
    emit(mid_end + ":")
    ins("tst.l 76(sp)")              # last
    ins("beq %s" % last_done)
    tile_setup(4)
    ins("moveq #0,d0")               # pixels 0 .. last - 1
    ins("move.l 76(sp),d2")
    partial_tile()
    emit(last_done + ":")
    ins("add.l #512,48(sp)")         # the next line (the arguments are this call's own)
    ins("addq.l #1,60(sp)")
    ins("subq.l #1,a3")
    ins("cmp.w #0,a3")
    ins("bne %s" % top)
    ins("movem.l (sp)+,d2-d7/a2-a6")
    ins("rts")


def txt_line():
    emit("", "_video_txt_lines_68k:")
    ins("movem.l d2-d7/a2-a6,-(sp)")
    top = L()
    ins("move.l 68(sp),a2")          # lines
    emit(top + ":")
    ins("move.l 48(sp),a3")          # line
    ins("move.l 52(sp),a1")          # cells
    ins("move.l 56(sp),d7")          # count
    ins("move.l 60(sp),d3")          # fy
    ins("move.l 64(sp),a4")          # opaque
    ins("move.l d3,d4")
    ins("lsl.l #3,d4")               # fy * 8
    loop, done, end = L(), L(), L()
    ins("subq.l #1,d7")
    ins("bmi %s" % end)
    emit(loop + ":")
    tile_setup(3)
    ins("lea (a3,d5.w),a0")          # x of the cell
    whole_tile(8, done)
    emit(done + ":")
    ins("dbra d7,%s" % loop)
    emit(end + ":")
    ins("add.l #512,48(sp)")
    ins("addq.l #1,60(sp)")
    ins("subq.l #1,a2")
    ins("cmp.w #0,a2")
    ins("bne %s" % top)
    ins("movem.l (sp)+,d2-d7/a2-a6")
    ins("rts")


def main():
    emit("; generated by tools/gen_video_68k.py - do not edit",
         "\tmachine\t68060", "\tsection\tcode",
         "\txdef\t_video_bg_lines_68k", "\txdef\t_video_txt_lines_68k")
    bg_line()
    txt_line()
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: %d lines" % (OUT, len(lines)))


if __name__ == "__main__":
    main()
