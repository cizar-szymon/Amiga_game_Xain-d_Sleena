; YM2203 FM output in 68k assembler (vasm, Motorola syntax) - the same as channel_output and
; the feedback path of clock_fm in ym2203.c (identical results, checked by tests/ymbench.c).
;
;   int32_t ym2203_fm_output_68k(YM2203 *ym, uint32_t t, int full)
;
; full != 0: the sum of the outputs of the active channels (fm_channel::output_4op), operator 1
; outputs stored in feedback_in; full = 0: only operator 1 of the active channels with feedback
; (the samples without output when fm_div > 1), returns 0.
;
; Struct offsets: checked against the C layout by ym2203_layout_ok().

	machine	68060

OP_PHASE	equ	0
OP_PT		equ	4
OP_STEP		equ	8
OP_EG4		equ	20
OP_LIVE		equ	37
CH_OP		equ	0
CH_FB0		equ	16
CH_FB1		equ	18
CH_FBIN		equ	20
CH_SIZE		equ	24
YM_ACTIVE	equ	263
YM_CH		equ	768

	section	code

	xdef	_ym2203_fm_output_68k
	xref	_ym2203_wave
	xref	_ym2203_power_table

; operator a2 with the phase input d1.l (signed) -> d0.l volume (signed)
; t in d7, wave table a3, power table a4; clobbers d2 d3
OPVOL	macro
	move.l	OP_EG4(a2),d3
	cmp.l	#$1000,d3
	bhs.s	.z\@			; silent
	move.l	OP_PHASE(a2),d0
	tst.b	OP_LIVE(a2)
	beq.s	.p\@
	move.l	d7,d2			; lazy phase: phase + step * (t - pt)
	sub.l	OP_PT(a2),d2
	mulu.l	OP_STEP(a2),d2
	add.l	d2,d0
.p\@	lsr.l	#8,d0
	lsr.l	#2,d0
	add.l	d1,d0
	and.l	#$3ff,d0
	move.w	(a3,d0.l*2),d2		; sign << 15 | sine attenuation
	moveq	#0,d0
	move.w	d2,d0
	and.w	#$7fff,d0
	add.l	d3,d0
	cmp.l	#$1000,d0
	bhs.s	.z\@
	move.l	d0,d3
	lsr.l	#8,d3
	and.l	#$ff,d0
	move.w	(a4,d0.l*2),d0		; upper word is 0
	lsr.l	d3,d0
	tst.w	d2
	bpl.s	.e\@
	neg.l	d0
	bra.s	.e\@
.z\@	moveq	#0,d0
.e\@
	endm

; operator n (0-3) of the channel a1 with the input d1 -> d0
OPN	macro
	move.l	CH_OP+4*\1(a1),a2
	OPVOL
	endm

; d1 = the operator 1 input (feedback) of channel a1, register B0 in d6
FBMOD	macro
	moveq	#0,d1
	move.l	d6,d3
	lsr.b	#3,d3
	and.l	#7,d3
	beq.s	.n\@
	move.w	CH_FB0(a1),d1
	ext.l	d1
	move.w	CH_FB1(a1),d2
	ext.l	d2
	add.l	d2,d1
	moveq	#10,d2
	sub.l	d3,d2
	asr.l	d2,d1
.n\@
	endm

_ym2203_fm_output_68k:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0		; ym
	move.l	52(sp),d7		; t
	move.l	56(sp),d0		; full
	lea	_ym2203_wave,a3
	lea	_ym2203_power_table,a4
	sub.l	a6,a6			; sum
	lea	YM_CH(a0),a1
	moveq	#0,d5			; channel
	tst.l	d0
	beq	.fbloop
.loop	btst	d5,YM_ACTIVE(a0)
	beq	.next
	moveq	#0,d6
	move.b	$b0(a0,d5.l),d6
	move.l	d5,-(sp)
	bsr	chan_out
	move.l	(sp)+,d5
	add.l	d0,a6
.next	lea	CH_SIZE(a1),a1
	addq.l	#1,d5
	cmp.l	#3,d5
	bne	.loop
	move.l	a6,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts

.fbloop	btst	d5,YM_ACTIVE(a0)
	beq	.fbnext
	moveq	#0,d6
	move.b	$b0(a0,d5.l),d6
	move.l	d6,d3
	and.b	#$38,d3
	beq	.fbnext			; no feedback
	FBMOD
	OPN	0
	move.w	d0,CH_FBIN(a1)
.fbnext	lea	CH_SIZE(a1),a1
	addq.l	#1,d5
	cmp.l	#3,d5
	bne	.fbloop
	moveq	#0,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts

; channel a1, register B0 in d6 -> d0 = output; o1 d4, o2 d5, o3 d6
chan_out:
	FBMOD
	OPN	0
	move.w	d0,CH_FBIN(a1)
	ext.l	d0			; (int16) as feedback_in (the value fits)
	move.l	d0,d4
	and.w	#7,d6
	move.w	.tab(pc,d6.w*2),d6
	jmp	.tab(pc,d6.w)
.tab	dc.w	.a0-.tab,.a1-.tab,.a2-.tab,.a3-.tab,.a4-.tab,.a5-.tab,.a6-.tab,.a7-.tab

.a0	move.l	d4,d1			; 1 -> 2 -> 3 -> 4
	asr.l	#1,d1
	OPN	1
	move.l	d0,d1
	asr.l	#1,d1
	OPN	2
	move.l	d0,d1
	asr.l	#1,d1
	OPN	3
	rts

.a1	moveq	#0,d1			; (1 + 2) -> 3 -> 4
	OPN	1
	move.l	d4,d1
	add.l	d0,d1
	asr.l	#1,d1
	OPN	2
	move.l	d0,d1
	asr.l	#1,d1
	OPN	3
	rts

.a2	moveq	#0,d1			; (1 + (2 -> 3)) -> 4
	OPN	1
	move.l	d0,d1
	asr.l	#1,d1
	OPN	2
	move.l	d4,d1
	add.l	d0,d1
	asr.l	#1,d1
	OPN	3
	rts

.a3	move.l	d4,d1			; ((1 -> 2) + 3) -> 4
	asr.l	#1,d1
	OPN	1
	move.l	d0,d5
	moveq	#0,d1
	OPN	2
	move.l	d5,d1
	add.l	d0,d1
	asr.l	#1,d1
	OPN	3
	rts

.a4	move.l	d4,d1			; (1 -> 2) + (3 -> 4)
	asr.l	#1,d1
	OPN	1
	move.l	d0,d5
	moveq	#0,d1
	OPN	2
	move.l	d0,d1
	asr.l	#1,d1
	OPN	3
	add.l	d5,d0
	rts

.a5	move.l	d4,d1			; 1 -> (2, 3, 4)
	asr.l	#1,d1
	OPN	1
	move.l	d0,d5
	move.l	d4,d1
	asr.l	#1,d1
	OPN	2
	move.l	d0,d6
	move.l	d4,d1
	asr.l	#1,d1
	OPN	3
	add.l	d5,d0
	add.l	d6,d0
	rts

.a6	move.l	d4,d1			; (1 -> 2) + 3 + 4
	asr.l	#1,d1
	OPN	1
	move.l	d0,d5
	moveq	#0,d1
	OPN	2
	move.l	d0,d6
	moveq	#0,d1
	OPN	3
	add.l	d5,d0
	add.l	d6,d0
	rts

.a7	moveq	#0,d1			; 1 + 2 + 3 + 4
	OPN	1
	move.l	d0,d5
	moveq	#0,d1
	OPN	2
	move.l	d0,d6
	moveq	#0,d1
	OPN	3
	add.l	d4,d0
	add.l	d5,d0
	add.l	d6,d0
	rts
