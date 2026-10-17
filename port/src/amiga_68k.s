; Vastar port - Amiga frontend helpers in 68k assembler (vasm, Motorola syntax)
;
; void copy_rows(const UBYTE *src, UBYTE *dst, ULONG bytes, ULONG rows,
;                ULONG src_mod, ULONG dst_mod, ULONG move16)
;
; Copies `rows` rows of `bytes` bytes from fast RAM to VRAM, only writing VRAM
; (see "Warp RTG Developer notes": never mix VRAM reads and writes, write wide).
; move16 != 0: src, dst, both modulos and bytes are multiples of 16, MOVE16 is used;
; otherwise long moves (bytes a multiple of 4).

	machine	68060
	section	"CODE",code

	public	_copy_rows
	cnop	0,4
_copy_rows:
	movem.l	d2-d6/a2-a3,-(sp)
	move.l	32(sp),a0		; src
	move.l	36(sp),a1		; dst
	move.l	40(sp),d0		; bytes
	move.l	44(sp),d1		; rows
	move.l	48(sp),d2		; src_mod
	move.l	52(sp),d3		; dst_mod
	tst.l	56(sp)
	beq.s	.longs
	tst.l	d1
	beq.s	.done
	lsr.l	#4,d0			; 16 byte lines per row
.r16:	move.l	a0,a2
	move.l	a1,a3
	move.l	d0,d4
.l16:	move16	(a2)+,(a3)+
	subq.l	#1,d4
	bne.s	.l16
	add.l	d2,a0
	add.l	d3,a1
	subq.l	#1,d1
	bne.s	.r16
	bra.s	.done
.longs:	tst.l	d1
	beq.s	.done
	lsr.l	#2,d0			; longs per row
.rl:	move.l	a0,a2
	move.l	a1,a3
	move.l	d0,d4
.ll:	move.l	(a2)+,(a3)+
	subq.l	#1,d4
	bne.s	.ll
	add.l	d2,a0
	add.l	d3,a1
	subq.l	#1,d1
	bne.s	.rl
.done:	movem.l	(sp)+,d2-d6/a2-a3
	rts

; void convert16(const UBYTE *src, UBYTE *dst, const UBYTE *flags, const void *lut,
;                ULONG dst_bpr, ULONG scale)
;
; One row of 8x8 blocks of palette indices (src = first of 8 rows of 224 bytes) into
; the 16 bit frame in fast RAM (dst = first row; scale 1: 16 bytes per block row,
; lut = UWORD[256]; scale 2: 32 bytes per block row and two rows per source row,
; lut = ULONG[256], the pixel twice). Blocks where flags[bx] == 0 are skipped.
	public	_convert16
	cnop	0,4
_convert16:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a2		; src
	move.l	52(sp),a3		; dst
	move.l	56(sp),a4		; flags
	move.l	60(sp),a5		; lut
	move.l	64(sp),d6		; dst_bpr
	moveq	#0,d7			; bx
	moveq	#0,d0
	cmp.l	#2,68(sp)
	beq	.s2
.b1:	tst.b	(a4,d7.l)
	beq.s	.n1
	move.l	d7,d1
	lsl.l	#3,d1
	lea	(a2,d1.l),a0		; src block
	add.l	d1,d1
	lea	(a3,d1.l),a1		; dst block (16 bytes per block row)
	moveq	#7,d2
.y1:	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	move.b	(a0)+,d0
	move.w	(a5,d0.w*2),(a1)+
	lea	224-8(a0),a0
	lea	-16(a1,d6.l),a1
	dbf	d2,.y1
.n1:	addq.l	#1,d7
	cmp.l	#28,d7
	blt.s	.b1
	bra	.done

.s2:	tst.b	(a4,d7.l)
	beq.s	.n2
	move.l	d7,d1
	lsl.l	#3,d1
	lea	(a2,d1.l),a0		; src block
	lsl.l	#2,d1
	lea	(a3,d1.l),a1		; dst block (32 bytes per block row)
	moveq	#7,d2
.y2:	lea	(a1,d6.l),a6		; the second row
	moveq	#7,d3
.x2:	move.b	(a0)+,d0
	move.l	(a5,d0.w*4),d4
	move.l	d4,(a1)+
	move.l	d4,(a6)+
	dbf	d3,.x2
	lea	224-8(a0),a0
	lea	-32(a1,d6.l),a1
	add.l	d6,a1			; two rows down
	dbf	d2,.y2
.n2:	addq.l	#1,d7
	cmp.l	#28,d7
	blt.s	.s2
.done:	movem.l	(sp)+,d2-d7/a2-a6
	rts

; ULONG find_dirty_68k(const UWORD *a, const UWORD *b, UBYTE *dirty)
;
; Xain'd Sleena: compares two 256x240 frames of 16 bit palette indices in 8x8 blocks
; (30 rows of 32); dirty[by * 32 + bx] = 1 where they differ. Returns the number of such blocks.

	public	_find_dirty_68k
	cnop	0,4
_find_dirty_68k:
	movem.l	d2-d7/a2-a4,-(sp)
	move.l	40(sp),a0		; a
	move.l	44(sp),a1		; b
	move.l	48(sp),a2		; dirty
	moveq	#0,d7			; count
	moveq	#29,d6			; block rows
.fbr:	moveq	#31,d5			; blocks in the row
.fbl:	move.l	a0,a3
	move.l	a1,a4
	moveq	#7,d4
.frow:	move.l	(a3),d0
	cmp.l	(a4),d0
	bne.s	.fdiff
	move.l	4(a3),d0
	cmp.l	4(a4),d0
	bne.s	.fdiff
	move.l	8(a3),d0
	cmp.l	8(a4),d0
	bne.s	.fdiff
	move.l	12(a3),d0
	cmp.l	12(a4),d0
	bne.s	.fdiff
	lea	512(a3),a3
	lea	512(a4),a4
	dbra	d4,.frow
	clr.b	(a2)+
	bra.s	.fnext
.fdiff:	move.b	#1,(a2)+
	addq.l	#1,d7
.fnext:	lea	16(a0),a0
	lea	16(a1),a1
	dbra	d5,.fbl
	lea	3584(a0),a0		; the other 7 lines of the block row
	lea	3584(a1),a1
	dbra	d6,.fbr
	move.l	d7,d0
	movem.l	(sp)+,d2-d7/a2-a4
	rts

; void convert_rows_68k(const UWORD *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels,
;                       const ULONG *lut, ULONG scale)
;
; Xain'd Sleena: 8 rows (512 bytes apart) of `pixels` (a multiple of 4) 16 bit palette
; indices into 16 bit screen pixels. scale 1: lut[i] low word, one row each; scale 2: lut[i]
; = the pixel doubled (two pixels per long word), each row written twice (dst, dst + dst_bpr);
; scale 3: as 2, but each row once (the caller copies it to both screen rows).

	public	_convert_rows_68k
	cnop	0,4
_convert_rows_68k:
	movem.l	d2-d7/a2-a5,-(sp)
	move.l	44(sp),a0		; src
	move.l	48(sp),a1		; dst
	move.l	52(sp),d5		; dst_bpr
	move.l	56(sp),d6		; pixels
	move.l	60(sp),a2		; lut
	lsr.l	#2,d6
	subq.l	#1,d6			; groups of 4 pixels - 1
	moveq	#7,d7
	cmp.l	#2,64(sp)
	beq.s	.c2
	cmp.l	#3,64(sp)
	beq	.c3
.c1row:	move.l	a0,a3
	move.l	a1,a4
	move.l	d6,d4
.c1px:	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.w	d1,(a4)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.w	d1,(a4)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.w	d1,(a4)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.w	d1,(a4)+
	dbra	d4,.c1px
	lea	512(a0),a0
	add.l	d5,a1
	dbra	d7,.c1row
	bra.s	.cdone
.c2:	move.l	a0,a3
	move.l	a1,a4
	lea	(a1,d5.l),a5
	move.l	d6,d4
.c2px:	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.l	d1,(a4)+
	move.l	d1,(a5)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.l	d1,(a4)+
	move.l	d1,(a5)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.l	d1,(a4)+
	move.l	d1,(a5)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),d1
	move.l	d1,(a4)+
	move.l	d1,(a5)+
	dbra	d4,.c2px
	lea	512(a0),a0
	add.l	d5,a1
	add.l	d5,a1
	dbra	d7,.c2
	bra.s	.cdone
.c3:	move.l	a0,a3
	move.l	a1,a4
	move.l	d6,d4
.c3px:	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),(a4)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),(a4)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),(a4)+
	move.w	(a3)+,d0
	move.l	(a2,d0.w*4),(a4)+
	dbra	d4,.c3px
	lea	512(a0),a0
	add.l	d5,a1
	dbra	d7,.c3
.cdone:	movem.l	(sp)+,d2-d7/a2-a5
	rts

; void convert_fast_68k(const uint16_t *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels, const void *lut, ULONG mode)
; 8 rows of palette indices (512 bytes per row, as a band or the picture) into the screen format,
; 4 pixels per turn with the table lookups interleaved (the 68060 waits when a loaded index is
; used at once). mode 1: 16 bit pixels, lut = UWORD[512]; mode 3: each pixel twice (SCALE=2),
; lut = ULONG[512] (the pixel in both words). pixels: a multiple of 4.
	public	_convert_fast_68k
	cnop	0,4
_convert_fast_68k:
	movem.l	d2-d7/a2-a5,-(sp)
	move.l	44(sp),a0		; src
	move.l	48(sp),a1		; dst
	move.l	52(sp),a5		; dst_bpr
	move.l	56(sp),d6		; pixels
	move.l	60(sp),a2		; lut
	lsr.l	#2,d6
	subq.l	#1,d6			; groups of 4 pixels - 1
	moveq	#7,d7
	cmp.l	#3,64(sp)
	beq.s	.f3
.f1row:	move.l	a0,a3
	move.l	a1,a4
	move.l	d6,d4
.f1px:	move.l	(a3)+,d0		; p0:p1
	move.l	(a3)+,d2		; p2:p3
	move.w	d0,d1
	swap	d0
	move.w	d2,d3
	swap	d2
	move.w	(a2,d0.w*2),d5
	swap	d5
	move.w	(a2,d1.w*2),d5
	move.w	(a2,d2.w*2),d0
	swap	d0
	move.w	(a2,d3.w*2),d0
	move.l	d5,(a4)+
	move.l	d0,(a4)+
	dbra	d4,.f1px
	lea	512(a0),a0
	add.l	a5,a1
	dbra	d7,.f1row
	bra.s	.fdone
.f3:	move.l	a0,a3
	move.l	a1,a4
	move.l	d6,d4
.f3px:	move.l	(a3)+,d0
	move.l	(a3)+,d2
	move.w	d0,d1
	swap	d0
	move.w	d2,d3
	swap	d2
	move.l	(a2,d0.w*4),(a4)+
	move.l	(a2,d1.w*4),(a4)+
	move.l	(a2,d2.w*4),(a4)+
	move.l	(a2,d3.w*4),(a4)+
	dbra	d4,.f3px
	lea	512(a0),a0
	add.l	a5,a1
	dbra	d7,.f3
.fdone:	movem.l	(sp)+,d2-d7/a2-a5
	rts

; ULONG hash_band_68k(const uint16_t *band, ULONG *hash, UBYTE *dirty, ULONG full)
; 32 blocks of a band (8 rows of 256 pixels, 512 bytes per row): per block a 32 bit FNV-1a hash
; of its 8 x 4 long words (h = (h ^ long) * 16777619, from 2166136261); dirty[bx] = 1 when it
; differs from hash[bx] (or full), hash[bx] = the new one. Returns the number of dirty blocks.
; (The first version, h = rol(h, 5) ^ long, gave a block of one colour only two hashes - the
; parity of the colour's bits - so a flat sky that changed colour was often not sent.)
	public	_hash_band_68k
	cnop	0,4
_hash_band_68k:
	movem.l	d2-d7/a2-a3,-(sp)
	move.l	36(sp),a0		; band
	move.l	40(sp),a1		; hash
	move.l	44(sp),a2		; dirty
	move.l	48(sp),d7		; full
	moveq	#0,d6			; dirty blocks
	move.l	#16777619,d3		; FNV prime
	moveq	#31,d5
.hblk:	move.l	a0,a3
	move.l	#$811c9dc5,d0
	moveq	#7,d4
.hrow:	move.l	(a3),d1
	move.l	4(a3),d2
	eor.l	d1,d0
	mulu.l	d3,d0
	move.l	8(a3),d1
	eor.l	d2,d0
	mulu.l	d3,d0
	move.l	12(a3),d2
	eor.l	d1,d0
	mulu.l	d3,d0
	eor.l	d2,d0
	mulu.l	d3,d0
	lea	512(a3),a3
	dbra	d4,.hrow
	moveq	#0,d1
	cmp.l	(a1),d0
	sne	d1
	or.b	d7,d1
	move.l	d0,(a1)+
	tst.b	d1
	sne	d1
	and.b	#1,d1
	move.b	d1,(a2)+
	add.l	d1,d6
	lea	16(a0),a0
	dbra	d5,.hblk
	move.l	d6,d0
	movem.l	(sp)+,d2-d7/a2-a3
	rts

; void double_rows_68k(const uint16_t *src, UBYTE *dst, ULONG dst_bpr, ULONG pixels)
; 8 rows of 16 bit screen pixels (512 bytes per source row) to VRAM at SCALE=2: every pixel
; twice in a long word, every row twice (dst_bpr = bytes per screen row). Long word writes only.
; pixels: a multiple of 2.
	public	_double_rows_68k
	cnop	0,4
_double_rows_68k:
	movem.l	d2-d7/a2-a4,-(sp)
	move.l	40(sp),a0		; src
	move.l	44(sp),a1		; dst
	move.l	48(sp),d5		; dst_bpr
	move.l	52(sp),d6		; pixels
	lsr.l	#1,d6
	subq.l	#1,d6			; pairs - 1
	moveq	#7,d7
.drow:	move.l	a0,a2
	move.l	a1,a3
	lea	(a1,d5.l),a4
	move.l	d6,d4
.dpx:	move.l	(a2)+,d0		; p0:p1
	move.l	d0,d3
	move.l	d0,d2
	swap	d0			; p1:p0
	move.w	d0,d3			; d3 = p0:p0
	move.w	d2,d0			; d0 = p1:p1
	move.l	d3,(a3)+
	move.l	d0,(a3)+
	move.l	d3,(a4)+
	move.l	d0,(a4)+
	dbra	d4,.dpx
	lea	512(a0),a0
	add.l	d5,a1
	add.l	d5,a1
	dbra	d7,.drow
	movem.l	(sp)+,d2-d7/a2-a4
	rts
