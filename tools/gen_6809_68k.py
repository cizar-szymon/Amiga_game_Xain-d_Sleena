#!/usr/bin/env python3
"""Generates port/src/m6809_68k.s: the 6809 core in 68k assembler (vasm, Motorola syntax).

    python tools/gen_6809_68k.py            -> port/src/m6809_68k.s

Same state and behaviour as port/src/m6809.c (the reference; struct layout in m6809.h, checked
by m6809_layout_ok()). Every instruction subtracts the same cycles as the C core, memory
accesses happen in the same order. Rare instructions (TFR, EXG, RTI, SWI, CWAI, SYNC, DAA,
the undocumented ones), interrupt entries and the idle loop check are done by the C core
(m6809_step, m6809_idle_hit): the asm core writes its registers into the struct, calls C and
reads them back.

68k registers during m6809_run_asm():
    d3  CC                  a2  rbase table (struct + 2048): host = rbase[page] + address
    d4  A                   a1  wbase table (struct + 3072) (restored after C calls)
    d5  B                   a3  PC base: rbase of the page of PC (guest PC = a4 - a3)
    d6  X (upper word 0)    a4  PC as a host pointer
    d7  cycles left         a5  the M6809 struct
    d0 d1 d2 a0 scratch     a6  opcode jump table (page 0)
S, Y, U and DP stay in the struct. Flags N Z V C come from the 68k CCR (same bit positions),
H is computed for ADD/ADC.
"""
import os
import re
import sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "port", "src", "m6809_68k.s")
# --trace: a debugging variant (m6809_68k_trace.s) that records every dispatched instruction
# (PC, A, B, X, CC, icount, CPU) in the ring buffer m6809_trace_buf (tests/lockstep.c, LockstepT)
TRACE = "--trace" in sys.argv
if TRACE:
    OUT = OUT.replace("m6809_68k.s", "m6809_68k_trace.s")

RBASE, WBASE = 2048, 3072
O_ICOUNT, O_TOTAL = 4112, 4116
O_X, O_Y, O_U, O_S, O_PC = 4160, 4162, 4164, 4166, 4168
O_A, O_B, O_DP, O_CC = 4190, 4191, 4192, 4193
O_NMIP, O_FIRQ, O_IRQ, O_LDS, O_WAIT, O_FREE = 4197, 4195, 4196, 4198, 4199, 4200
O_IVALID, O_IMAP = 4210, 4224
O_TRMAP = 12420                 # translated code: map PC 8000-FFFF -> host code (NULL = none)

# template mode (gen_templates): the handlers are generated again as templates for the
# translator (m6809_tr.c): next_op leaves the marker @NEXT instead of the dispatch
TPL = False

lines = []
label_no = [0]


def emit(*ls):
    lines.extend(ls)


def ins(s):
    lines.append("\t" + s)


def L():
    label_no[0] += 1
    return "L%d" % label_no[0]


# ---------------------------------------------------------------- memory

def rd8(dst="d1"):
    """byte at address d0.l (upper word 0) into dst; keeps d0; clobbers d2 a0"""
    slow, done = L(), L()
    ins("move.w d0,d2")
    ins("lsr.w #8,d2")
    ins("move.l (a2,d2.w*4),d2")
    ins("beq.s " + slow)
    ins("move.l d2,a0")
    ins("move.b (a0,d0.l),%s" % dst)
    ins("bra.s " + done)
    emit(slow + ":")
    ins("bsr rd_slow")
    if dst != "d1":
        ins("move.b d1,%s" % dst)
    emit(done + ":")


def rd16():
    """word at d0.l (big endian, high byte first) into d1.w; keeps d0; clobbers d2 a0"""
    slow, done = L(), L()
    ins("move.w d0,d2")
    ins("lsr.w #8,d2")
    ins("move.l (a2,d2.w*4),d2")
    ins("beq.s " + slow)
    ins("cmp.b #$ff,d0")
    ins("beq.s " + slow)
    ins("move.l d2,a0")
    ins("move.w (a0,d0.l),d1")
    ins("bra.s " + done)
    emit(slow + ":")
    ins("bsr rd16_slow")
    emit(done + ":")


def wr8():
    """byte d1.b to address d0.l; keeps d0 d1; clobbers d2 a0"""
    slow, done = L(), L()
    ins("move.w d0,d2")
    ins("lsr.w #8,d2")
    ins("move.l (a1,d2.w*4),d2")
    ins("beq.s " + slow)
    ins("move.l d2,a0")
    ins("move.b d1,(a0,d0.l)")
    ins("bra.s " + done)
    emit(slow + ":")
    ins("bsr wr_slow")
    emit(done + ":")


def wr16():
    """word d1.w to d0.l (high byte first); keeps d0 d1; clobbers d2 a0"""
    slow, done = L(), L()
    ins("move.w d0,d2")
    ins("lsr.w #8,d2")
    ins("move.l (a1,d2.w*4),d2")
    ins("beq.s " + slow)
    ins("cmp.b #$ff,d0")
    ins("beq.s " + slow)
    ins("move.l d2,a0")
    ins("move.w d1,(a0,d0.l)")
    ins("bra.s " + done)
    emit(slow + ":")
    ins("bsr wr16_slow")
    emit(done + ":")


def get_pc(dst):
    ins("move.l a4,%s" % dst)
    ins("sub.l a3,%s" % dst)


def set_pc(src):
    """src: data register (not d2) with the address in the low word, upper word 0"""
    ins("move.w %s,d2" % src)
    ins("lsr.w #8,d2")
    ins("move.l (a2,d2.w*4),a3")
    ins("lea (a3,%s.l),a4" % src)


def rel_jump():
    """PC += d0.w (16 bit, wrapping): a long branch can reach another memory region (fixed ROM
    from a ROM bank and back), where the host memory is not contiguous, so the target is mapped
    again (set_pc). Short branches only use lea: like sequential execution they assume that
    code does not run over the end of a region (true for the game). Clobbers d0 d2."""
    ins("move.l a4,d2")
    ins("sub.l a3,d2")
    ins("add.w d2,d0")
    ins("and.l #$ffff,d0")
    set_pc("d0")


def sub_cycles(n):
    if n <= 0:
        return
    if TPL == 2:                                      # specialized templates: the translator counts
        ins("@CYC %d" % n)
        return
    ins("subq.l #%d,d7" % n if n <= 8 else "sub.l #%d,d7" % n)


def next_op(cycles):
    if TPL == 2:
        sub_cycles(cycles)
        ins("@NEXT")
        return
    sub_cycles(cycles)
    if cycles <= 0:
        ins("tst.l d7")
    ins("ble exit_loop")
    if TPL:
        ins("@NEXT")
        return
    if TRACE:
        ins("bsr trace_rec")
    ins("moveq #0,d0")
    ins("move.b (a4)+,d0")
    ins("move.l (a6,d0.w*4),a0")
    ins("jmp (a0)")


def go_check_ints(cycles):
    if TPL == 2:
        ins("@CHECK")
        return
    sub_cycles(cycles)
    ins("bra check_ints")


# ---------------------------------------------------------------- flags

def flags(mask):
    """CC bits in `mask` (of N Z V C) from the 68k CCR, the others kept"""
    ins("move.w ccr,d2")
    ins("and.b #$%02x,d2" % mask)
    ins("and.b #$%02x,d3" % (0xff & ~mask))
    ins("or.b d2,d3")


def x_from_c(set_z=False):
    """68k X (and C) = 6809 C; with set_z also Z = 1 (for ADDX/SUBX)"""
    ins("move.b d3,d2")
    ins("and.b #1,d2")
    ins("lsl.b #4,d2")
    if set_z:
        ins("or.b #4,d2")
    ins("move.w d2,ccr")


# ---------------------------------------------------------------- effective addresses
# Each returns the address in d0.l (upper word 0) and subtracts its cycles (not the opcode's).

def ea_direct():
    if TPL == 2:                                      # the translator inserts the address piece
        ins("@EA1")
        return 2
    ins("moveq #0,d0")
    ins("move.w %d(a5),d0" % O_DP)                    # DP:CC, the low byte is replaced
    ins("move.b (a4)+,d0")
    return 2


def ea_extended():
    if TPL == 2:
        ins("@EA3")
        return 3
    ins("moveq #0,d0")
    ins("move.w (a4)+,d0")
    return 3


# the most frequent postbytes with bit 7 set (Xain'd Sleena, all CPUs: ,X+ ,Y++ ,Y ,X++ ,Y+ ,S ,X
# are 70% of the indexed instructions; the 5 bit offsets 00-7F another 12%), done inline
IDX_INLINE = [0x80, 0xa1, 0xa4, 0x81, 0xa0, 0xe4, 0x84]


def ea_indexed():
    """the effective address of the postbyte in d0 (its cycles subtracted): 5 bit offsets and
    the frequent postbytes inline, the others through the handler idx_XX (jsr, rts)"""
    if TPL == 2:                                      # the piece of the postbyte, its own cycles
        ins("@EA2")
        return 0
    done, hi, xreg, add = L(), L(), L(), L()
    ins("moveq #0,d0")
    ins("move.b (a4)+,d0")
    ins("bmi.s " + hi)
    # 00-7F: register (bits 5-6) + 5 bit signed offset, 3 cycles with the postbyte
    ins("move.w d0,d2")
    ins("and.w #$60,d2")
    ins("beq.s " + xreg)
    ins("lsr.w #4,d2")                                # register * 2: Y U S at %d.. in the struct
    ins("move.w %d(a5,d2.w),d2" % O_X)
    ins("bra.s " + add)
    emit(xreg + ":")
    ins("move.w d6,d2")
    emit(add + ":")
    ins("lsl.b #3,d0")
    ins("asr.b #3,d0")
    ins("ext.w d0")
    ins("add.w d2,d0")
    sub_cycles(3)
    ins("bra " + done)
    emit(hi + ":")
    for post in IDX_INLINE:
        nxt = L()
        ins("cmp.b #$%02x,d0" % post)
        ins("bne " + nxt)
        idx_body(post)
        ins("bra " + done)
        emit(nxt + ":")
    ins("lea idxtab,a0")
    ins("move.l (a0,d0.w*4),a0")
    ins("jsr (a0)")
    emit(done + ":")
    return 0


def ea_mode(mode):
    """mode 1 direct, 2 indexed, 3 extended"""
    return {1: ea_direct, 2: ea_indexed, 3: ea_extended}[mode]()


IREG = {0: "X", 1: "Y", 2: "U", 3: "S"}
IREG_OFS = {"Y": O_Y, "U": O_U, "S": O_S}


def ireg_load(r, dst):
    if r == "X":
        ins("moveq #0,%s" % dst)
        ins("move.w d6,%s" % dst)
    else:
        ins("moveq #0,%s" % dst)
        ins("move.w %d(a5),%s" % (IREG_OFS[r], dst))


def ireg_add(r, n):
    op = "addq.w" if n > 0 else "subq.w"
    if r == "X":
        ins("%s #%d,d6" % (op, abs(n)))
    else:
        ins("%s #%d,%d(a5)" % (op, abs(n), IREG_OFS[r]))


def gen_idx(post):
    """handler idx_XX: d0 = effective address, subtracts the cycles of the mode"""
    emit("idx_%02X:" % post)
    idx_body(post)
    ins("rts")


def idx_body(post):
    """the effective address of postbyte `post` into d0.l (upper word 0), cycles subtracted;
    clobbers d2 (and a0 for indirect reads)"""
    r = IREG[(post >> 5) & 3]
    if not post & 0x80:
        off = (post & 0x0f) | (0xf0 if post & 0x10 else 0)
        off = off - 256 if off & 0x80 else off
        ireg_load(r, "d0")
        if off:
            ins("add.w #%d,d0" % off)
        sub_cycles(3)
        return
    mode = post & 0x0f
    cyc = 0
    if mode == 0x00:
        ireg_load(r, "d0"); ireg_add(r, 1); cyc = 3
    elif mode == 0x01:
        ireg_load(r, "d0"); ireg_add(r, 2); cyc = 4
    elif mode == 0x02:
        ireg_add(r, -1); ireg_load(r, "d0"); cyc = 3
    elif mode == 0x03:
        ireg_add(r, -2); ireg_load(r, "d0"); cyc = 4
    elif mode == 0x04:
        ireg_load(r, "d0"); cyc = 1
    elif mode in (0x05, 0x06):
        ins("move.b %s,d2" % ("d5" if mode == 0x05 else "d4"))
        ins("ext.w d2")
        ireg_load(r, "d0")
        ins("add.w d2,d0")
        cyc = 2
    elif mode == 0x08:
        ins("move.b (a4)+,d2")
        ins("ext.w d2")
        ireg_load(r, "d0")
        ins("add.w d2,d0")
        cyc = 2
    elif mode == 0x09:
        ins("move.w (a4)+,d2")
        ireg_load(r, "d0")
        ins("add.w d2,d0")
        cyc = 5
    elif mode == 0x0b:
        ins("move.b d4,d2")
        ins("lsl.w #8,d2")
        ins("move.b d5,d2")
        ireg_load(r, "d0")
        ins("add.w d2,d0")
        cyc = 5
    elif mode == 0x0c:
        ins("move.b (a4)+,d2")
        ins("ext.w d2")
        get_pc("d0")
        ins("add.w d2,d0")
        cyc = 2
    elif mode == 0x0d:
        ins("move.w (a4)+,d2")
        get_pc("d0")
        ins("add.w d2,d0")
        cyc = 6
    elif mode == 0x0f:
        ins("moveq #0,d0")
        ins("move.w (a4)+,d0")
        cyc = 3
    else:                                             # 07, 0A, 0E: undefined, address 0
        ins("moveq #0,d0")
        cyc = 0
    sub_cycles(cyc + 1)                               # + the postbyte fetch
    if post & 0x10:                                   # indirect
        rd16()
        ins("moveq #0,d0")
        ins("move.w d1,d0")
        sub_cycles(3)


# ---------------------------------------------------------------- instructions

C_FALLBACK = set()


def handler(name):
    emit("", "%s:" % name)


def fallback(name, prefix=False):
    """execute by the C core (pc back at the opcode or the prefix)"""
    handler(name)
    ins("subq.l #%d,a4" % (2 if prefix else 1))
    ins("bra c_step")


def rmw_body(fn, reg):
    """the operation fn (low nibble of 00-0F/40-7F) on d1.b (memory) or reg; flags into d3"""
    r = reg or "d1"
    if fn in (0x0, 0x1):
        ins("neg.b %s" % r)
        flags(0x0f)
    elif fn == 0x3:
        ins("not.b %s" % r)
        flags(0x0e)
        ins("or.b #1,d3")
    elif fn in (0x4, 0x5):
        ins("lsr.b #1,%s" % r)
        flags(0x0d)
    elif fn == 0x6:
        x_from_c()
        ins("roxr.b #1,%s" % r)
        flags(0x0d)
    elif fn == 0x7:
        ins("asr.b #1,%s" % r)
        flags(0x0d)
    elif fn == 0x8:
        ins("asl.b #1,%s" % r)
        flags(0x0f)
    elif fn == 0x9:
        n0, c0 = L(), L()
        x_from_c()
        ins("roxl.b #1,%s" % r)
        ins("move.w ccr,d2")
        ins("and.b #$0d,d2")
        ins("btst #3,d2")
        ins("beq.s " + n0)
        ins("bchg #1,d2")
        emit(n0 + ":")
        ins("btst #0,d2")
        ins("beq.s " + c0)
        ins("bchg #1,d2")
        emit(c0 + ":")
        ins("and.b #$f0,d3")
        ins("or.b d2,d3")
    elif fn == 0xa:
        ins("subq.b #1,%s" % r)
        flags(0x0e)
    elif fn == 0xc:
        ins("addq.b #1,%s" % r)
        flags(0x0e)
    elif fn == 0xd:
        ins("tst.b %s" % r)
        flags(0x0e)
    elif fn == 0xf:
        ins("moveq #0,%s" % r if r.startswith("d") and reg is None else "clr.b %s" % r)
        ins("and.b #$f0,d3")
        ins("or.b #4,d3")
    else:
        raise ValueError(fn)


RMW_OK = {0x0, 0x1, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xc, 0xd, 0xf}


def gen_rmw(op):
    hi, fn = op >> 4, op & 0x0f
    name = "op_%02X" % op
    if hi in (4, 5):                                  # A / B
        if fn not in RMW_OK:
            fallback(name)
            return
        handler(name)
        rmw_body(fn, "d4" if hi == 4 else "d5")
        next_op(2)
        return
    mode = {0: 1, 6: 2, 7: 3}[hi]
    if fn == 0xe:                                     # JMP
        handler(name)
        cyc = 1 + ea_mode(mode)
        idle_target_jump("d0", cyc)
        return
    if fn not in RMW_OK:
        fallback(name)
        return
    handler(name)
    cyc = 1 + ea_mode(mode)
    if fn == 0xf:                                     # CLR: read, dummy, write 0
        rd8()
        ins("moveq #0,d1")
        wr8()
        ins("and.b #$f0,d3")
        ins("or.b #4,d3")
        next_op(cyc + 3)
        return
    rd8()
    rmw_body(fn, None)
    if fn == 0xd:                                     # TST: no write
        next_op(cyc + 3)
        return
    wr8()
    next_op(cyc + 3)


def idle_target_jump(src, cyc):
    """jump to the address in src (data register, upper word 0): set PC, check the idle map"""
    set_pc(src)
    sub_cycles(cyc)
    ins("move.w %s,d1" % src)
    ins("lsr.w #3,d1")
    ins("btst %s,(%d,a5,d1.w)" % (src, O_IMAP))
    ins("bne idle_hit")
    next_op(0)


def gen_branch(op):
    """20-2F: BRA..BLE; 21 BRN"""
    handler("op_%02X" % op)
    cc = ["ra", "rn", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"][op & 15]
    not_taken = L()
    ins("move.b (a4)+,d0")
    if cc == "rn":
        next_op(3)
        return
    if cc != "ra":
        ins("move.w d3,ccr")
        inv = {"hi": "ls", "ls": "hi", "cc": "cs", "cs": "cc", "ne": "eq", "eq": "ne", "vc": "vs", "vs": "vc",
               "pl": "mi", "mi": "pl", "ge": "lt", "lt": "ge", "gt": "le", "le": "gt"}[cc]
        ins("b%s.s %s" % (inv, not_taken))
    taken_back = L()
    ins("ext.w d0")
    ins("lea (a4,d0.w),a4")
    ins("tst.w d0")
    ins("bmi.s " + taken_back)
    next_op(3)
    emit(taken_back + ":")                            # backward: an idle loop?
    get_pc("d0")
    sub_cycles(3)
    ins("move.w d0,d1")
    ins("lsr.w #3,d1")
    ins("btst d0,(%d,a5,d1.w)" % O_IMAP)
    ins("bne idle_hit")
    next_op(0)
    if cc != "ra":
        emit(not_taken + ":")
        next_op(3)


def gen_lbranch(name, cc, prefix_cycles):
    """long branch: 16 LBRA (cc None), 10 2x LBcc"""
    handler(name)
    not_taken = L()
    ins("move.w (a4)+,d0")
    if cc == "rn":
        next_op(prefix_cycles + 4)
        return
    if cc is not None:
        ins("move.w d3,ccr")
        inv = {"hi": "ls", "ls": "hi", "cc": "cs", "cs": "cc", "ne": "eq", "eq": "ne", "vc": "vs", "vs": "vc",
               "pl": "mi", "mi": "pl", "ge": "lt", "lt": "ge", "gt": "le", "le": "gt", "ra": None}[cc]
        if inv:
            ins("b%s.s %s" % (inv, not_taken))
    rel_jump()
    next_op(prefix_cycles + 5)
    if cc is not None:
        emit(not_taken + ":")
        next_op(prefix_cycles + 4)


ALU8 = {0x0: "sub", 0x1: "cmp", 0x2: "sbc", 0x4: "and", 0x5: "bit", 0x6: "ld", 0x8: "eor",
        0x9: "adc", 0xa: "or", 0xb: "add"}


def alu8_body(kind, reg):
    """operand in d1.b, register reg"""
    if kind == "sub":
        ins("sub.b d1,%s" % reg); flags(0x0f)
    elif kind == "cmp":
        ins("cmp.b d1,%s" % reg); flags(0x0f)
    elif kind == "sbc":
        x_from_c(True); ins("subx.b d1,%s" % reg); flags(0x0f)
    elif kind == "and":
        ins("and.b d1,%s" % reg); flags(0x0e)
    elif kind == "bit":
        ins("move.b %s,d2" % reg); ins("and.b d1,d2"); flags(0x0e)
    elif kind == "ld":
        ins("move.b d1,%s" % reg); flags(0x0e)
    elif kind == "eor":
        ins("eor.b d1,%s" % reg); flags(0x0e)
    elif kind == "or":
        ins("or.b d1,%s" % reg); flags(0x0e)
    elif kind in ("add", "adc"):
        ins("move.b %s,d0" % reg)                     # the operand address is no longer needed
        if kind == "adc":
            x_from_c(True)
            ins("addx.b d1,%s" % reg)
        else:
            ins("add.b d1,%s" % reg)
        ins("move.w ccr,d2")
        ins("and.b #$0f,d2")
        ins("eor.b d1,d0")
        ins("eor.b %s,d0" % reg)
        ins("and.b #$10,d0")
        ins("add.b d0,d0")                            # H: bit 4 of a ^ b ^ r -> bit 5
        ins("and.b #$d0,d3")
        ins("or.b d2,d3")
        ins("or.b d0,d3")


def get_d(dst):
    ins("move.b d4,%s" % dst)
    ins("lsl.w #8,%s" % dst)
    ins("move.b d5,%s" % dst)


def set_d(src):
    """D from src.w (src is not d2; keeps the flags? no: callers take flags before)"""
    ins("move.b %s,d5" % src)
    ins("move.w %s,d2" % src)
    ins("lsr.w #8,d2")
    ins("move.b d2,d4")


def operand16(mode):
    """16 bit operand into d1.w; returns the address mode cycles"""
    if mode == 0:
        ins("move.w (a4)+,d1")
        return 2
    cyc = ea_mode(mode)
    rd16()
    return cyc + 2


def gen_page0_alu(op):
    hi, lo = op >> 4, op & 0x0f
    mode = hi & 3
    bside = hi >= 0x0c
    reg = "d5" if bside else "d4"
    name = "op_%02X" % op
    if lo in ALU8:
        handler(name)
        if mode == 0:
            ins("move.b (a4)+,d1")
            cyc = 2
        else:
            cyc = 1 + ea_mode(mode)
            rd8()
            cyc += 1
        alu8_body(ALU8[lo], reg)
        next_op(cyc)
        return
    if lo == 0x7:                                     # ST8 (immediate: XST8 -> C)
        if mode == 0:
            fallback(name)
            return
        handler(name)
        cyc = 1 + ea_mode(mode)
        ins("move.b %s,d1" % reg)
        flags(0x0e)
        wr8()
        next_op(cyc + 1)
        return
    if lo == 0x3:                                     # SUBD / ADDD
        handler(name)
        cyc = 1 + operand16(mode)
        get_d("d0")
        ins("%s.w d1,d0" % ("add" if bside else "sub"))
        flags(0x0f)
        set_d("d0")
        next_op(cyc + 1)
        return
    if lo == 0xc:
        handler(name)
        cyc = 1 + operand16(mode)
        if bside:                                     # LDD
            set_d("d1")
            ins("tst.w d1")
            flags(0x0e)
            next_op(cyc)
        else:                                         # CMPX
            ins("cmp.w d1,d6")
            flags(0x0f)
            next_op(cyc + 1)
        return
    if lo == 0xd:
        if bside:
            if mode == 0:                             # CD: free run -> C
                fallback(name)
                return
            handler(name)                             # STD
            cyc = 1 + ea_mode(mode)
            get_d("d1")
            ins("tst.w d1")
            flags(0x0e)
            wr16()
            next_op(cyc + 2)
            return
        handler(name)
        if mode == 0:                                 # BSR
            ins("move.b (a4)+,d0")
            ins("ext.w d0")
            get_pc("d1")
            ins("lea (a4,d0.w),a4")
            push_word_d1()
            next_op(1 + 1 + 3 + 2)
            return
        cyc = 1 + ea_mode(mode)                       # JSR
        get_pc("d1")
        set_pc("d0")
        push_word_d1()
        next_op(cyc + 2 + 2)
        return
    if lo == 0xe:                                     # LDX / LDU
        handler(name)
        cyc = 1 + operand16(mode)
        if bside:
            ins("move.w d1,%d(a5)" % O_U)
        else:
            ins("move.w d1,d6")
        flags(0x0e)
        next_op(cyc)
        return
    if lo == 0xf:                                     # STX / STU (immediate: XST16 -> C)
        if mode == 0:
            fallback(name)
            return
        handler(name)
        cyc = 1 + ea_mode(mode)
        if bside:
            ins("move.w %d(a5),d1" % O_U)
        else:
            ins("move.w d6,d1")
        flags(0x0e)
        wr16()
        next_op(cyc + 2)
        return
    raise ValueError(op)


def push_word_d1():
    """push d1.w on S (low byte first); clobbers d0 d2 a0"""
    ins("moveq #0,d0")
    ins("move.w %d(a5),d0" % O_S)
    ins("subq.w #1,d0")
    wr8()
    ins("subq.w #1,d0")
    ins("ror.w #8,d1")
    wr8()
    ins("move.w d0,%d(a5)" % O_S)


def gen_push(op, user):
    """34 PSHS / 36 PSHU: order PC, U/S, Y, X, DP, B, A, CC"""
    handler("op_%02X" % op)
    sp = O_U if user else O_S
    other = O_S if user else O_U
    ins("move.b (a4)+,d1")
    ins("move.b d1,-(sp)")                            # the mask
    ins("moveq #0,d0")
    ins("move.w %d(a5),d0" % sp)
    sub_cycles(5)
    steps = [(0x80, "pc"), (0x40, "other"), (0x20, "y"), (0x10, "x"), (0x08, "dp"), (0x04, "b"), (0x02, "a"), (0x01, "cc")]
    for bit, what in steps:
        skip = L()
        ins("btst #%d,(sp)" % (bit.bit_length() - 1))
        ins("beq.s " + skip)
        if what in ("pc", "other", "y", "x"):
            if what == "pc":
                get_pc("d1")
            elif what == "other":
                ins("move.w %d(a5),d1" % other)
            elif what == "y":
                ins("move.w %d(a5),d1" % O_Y)
            else:
                ins("move.w d6,d1")
            ins("subq.w #1,d0")
            wr8()
            ins("subq.w #1,d0")
            ins("ror.w #8,d1")
            wr8()
            sub_cycles(2)
        else:
            src = {"dp": "%d(a5)" % O_DP, "b": "d5", "a": "d4", "cc": "d3"}[what]
            ins("move.b %s,d1" % src)
            ins("subq.w #1,d0")
            wr8()
            sub_cycles(1)
        emit(skip + ":")
    ins("move.w d0,%d(a5)" % sp)
    ins("addq.l #2,sp")
    next_op(0)


def gen_pull(op, user):
    """35 PULS / 37 PULU: order CC, A, B, DP, X, Y, U/S, PC; then a dummy read"""
    handler("op_%02X" % op)
    sp = O_U if user else O_S
    other = O_S if user else O_U
    ins("move.b (a4)+,d1")
    ins("move.b d1,-(sp)")
    ins("moveq #0,d0")
    ins("move.w %d(a5),d0" % sp)
    sub_cycles(5)
    pull_steps(sp, other)
    # the final read of the stack (RAM: no side effect) is only counted
    ins("btst #0,(sp)+")                              # CC pulled: interrupts may be enabled
    no_cc = L()
    ins("beq.s " + no_cc)
    go_check_ints(0)
    emit(no_cc + ":")
    next_op(0)


def pull_steps(sp, other):
    """the registers of the mask at (sp) from the stack (address in d0, struct field sp), order
    CC, A, B, DP, X, Y, U/S, PC; 1 cycle per byte; S/U stored at the end"""
    steps = [(0x01, "cc"), (0x02, "a"), (0x04, "b"), (0x08, "dp"), (0x10, "x"), (0x20, "y"), (0x40, "other"), (0x80, "pc")]
    for bit, what in steps:
        skip = L()
        ins("btst #%d,(sp)" % (bit.bit_length() - 1))
        ins("beq.s " + skip)
        if what in ("cc", "a", "b", "dp"):
            rd8()
            ins("addq.w #1,d0")
            dst = {"cc": "d3", "a": "d4", "b": "d5", "dp": "%d(a5)" % O_DP}[what]
            ins("move.b d1,%s" % dst)
            sub_cycles(1)
        else:
            rd8()
            ins("lsl.w #8,d1")
            ins("addq.w #1,d0")
            rd8()
            ins("addq.w #1,d0")
            if what == "x":
                ins("move.w d1,d6")
            elif what == "y":
                ins("move.w d1,%d(a5)" % O_Y)
            elif what == "other":
                ins("move.w d1,%d(a5)" % other)
            else:
                ins("move.w d0,%d(a5)" % sp)          # PC: the new S first, set_pc clobbers d0
                ins("moveq #0,d0")
                ins("move.w d1,d0")
                set_pc("d0")
                ins("moveq #0,d0")
                ins("move.w %d(a5),d0" % sp)
            sub_cycles(2)
        emit(skip + ":")
    ins("move.w d0,%d(a5)" % sp)


def gen_rti():
    """3B RTI: CC from S, then (E set) A B DP X Y U PC or (E clear) PC; 6 or 15 cycles; the
    pulled CC may enable interrupts (MAME: EAT 1, the reads, a dummy read)"""
    handler("op_3B")
    ins("moveq #0,d0")
    ins("move.w %d(a5),d0" % O_S)
    rd8()
    ins("move.b d1,d3")
    ins("addq.w #1,d0")
    ins("move.b #$80,d1")                            # E clear: PC only
    ins("btst #7,d3")
    ins("beq.s *+6")
    ins("move.b #$fe,d1")                            # E set: A B DP X Y U PC
    ins("move.b d1,-(sp)")
    sub_cycles(4)
    pull_steps(O_S, O_U)
    ins("addq.l #2,sp")
    go_check_ints(0)


def gen_tfr_exg(op):
    """1E EXG (8 cycles), 1F TFR (6): registers through tfr_get / tfr_set (as m6809.c tfr_read,
    tfr_write); then interrupts are checked (CC may have changed), PC may have been written"""
    handler("op_%02X" % op)
    ins("moveq #0,d1")
    ins("move.b (a4)+,d1")
    if op == 0x1f:
        ins("move.w d1,-(sp)")                       # postbyte
        ins("lsr.b #4,d1")
        ins("moveq #0,d2")                           # not EXG: 8 bit sources as hi:lo copies
        ins("bsr tfr_get")
        ins("move.w (sp)+,d1")
        ins("and.w #$0f,d1")
        ins("bsr tfr_set")
        ins("cmp.b #4,d1")
        ins("bne.s *+8")
        ins("move.b #1,%d(a5)" % O_LDS)             # TFR to S: NMI enabled
        go_check_ints(6)
    else:
        ins("move.w d1,-(sp)")                       # postbyte
        ins("moveq #0,d2")
        ins("btst #7,d1")
        ins("seq d2")                                # 8/16 bit exchange (bit 7 clear): FF in the high byte
        ins("move.w d2,-(sp)")
        ins("lsr.b #4,d1")
        ins("bsr tfr_get")                           # r1
        ins("move.w d0,-(sp)")
        ins("move.w 4(sp),d1")
        ins("and.w #$0f,d1")
        ins("move.w 2(sp),d2")
        ins("bsr tfr_get")                           # r2
        ins("move.w d0,2(sp)")                       # (the flag is no longer needed)
        ins("move.w (sp)+,d0")                       # r1 -> register 2
        ins("move.w 2(sp),d1")
        ins("and.w #$0f,d1")
        ins("bsr tfr_set")
        ins("move.w (sp)+,d0")                       # r2 -> register 1
        ins("move.w (sp)+,d1")
        ins("lsr.b #4,d1")
        ins("bsr tfr_set")
        go_check_ints(8)


def gen_tfr_helpers():
    """tfr_get: register d1 (0-15) -> d0.w, d2.b != 0: 8 bit registers of EXG 8/16 as FF:value;
    tfr_set: d0.w -> register d1 (kept). Both may clobber d2 a0."""
    emit("", "tfr_get:")
    ins("lea tfr_gtab(pc),a0")
    ins("move.l (a0,d1.w*4),a0")
    ins("jmp (a0)")
    regs = {0: "d", 1: "x", 2: "y", 3: "u", 4: "s", 5: "pc", 8: "a", 9: "b", 10: "cc", 11: "dp"}
    labels = []
    for r in range(16):
        lab = "tg_%d" % r
        labels.append(lab)
        emit(lab + ":")
        w = regs.get(r)
        if w == "d":
            ins("move.b d4,d0"); ins("lsl.w #8,d0"); ins("move.b d5,d0")
        elif w == "x":
            ins("move.w d6,d0")
        elif w in ("y", "u", "s"):
            ins("move.w %d(a5),d0" % {"y": O_Y, "u": O_U, "s": O_S}[w])
        elif w == "pc":
            ins("move.l a4,d0"); ins("sub.l a3,d0")
        elif w in ("a", "b"):
            ins("move.w #$ff00,d0"); ins("move.b %s,d0" % ("d4" if w == "a" else "d5"))
        elif w in ("cc", "dp"):
            src = "d3" if w == "cc" else "%d(a5)" % O_DP
            ins("move.b %s,d0" % src)
            ins("lsl.w #8,d0")
            ins("move.b %s,d0" % src)
            ins("tst.b d2")
            ins("beq.s *+6")
            ins("or.w #$ff00,d0")
        else:
            ins("move.w #$ffff,d0")
        ins("rts")
    emit("	cnop	0,4", "tfr_gtab:")
    emit("	dc.l " + ",".join(labels))
    emit("", "tfr_set:")
    ins("lea tfr_stab(pc),a0")
    ins("move.l (a0,d1.w*4),a0")
    ins("jmp (a0)")
    labels = []
    for r in range(16):
        lab = "ts_%d" % r
        labels.append(lab)
        emit(lab + ":")
        w = regs.get(r)
        if w == "d":
            ins("move.b d0,d5"); ins("move.w d0,d2"); ins("lsr.w #8,d2"); ins("move.b d2,d4")
        elif w == "x":
            ins("move.w d0,d6")
        elif w in ("y", "u", "s"):
            ins("move.w d0,%d(a5)" % {"y": O_Y, "u": O_U, "s": O_S}[w])
        elif w == "pc":
            ins("and.l #$ffff,d0")
            set_pc("d0")
        elif w == "a":
            ins("move.b d0,d4")
        elif w == "b":
            ins("move.b d0,d5")
        elif w == "cc":
            ins("move.b d0,d3")
        elif w == "dp":
            ins("move.b d0,%d(a5)" % O_DP)
        ins("rts")
    emit("	cnop	0,4", "tfr_stab:")
    emit("	dc.l " + ",".join(labels))


def gen_page0():
    for op in range(256):
        hi, lo = op >> 4, op & 0x0f
        name = "op_%02X" % op
        if hi in (0, 4, 5, 6, 7):
            gen_rmw(op)
        elif hi == 2:
            gen_branch(op)
        elif op >= 0x80:
            gen_page0_alu(op)
        elif op == 0x10:
            handler(name)
            ins("moveq #0,d0")
            ins("move.b (a4)+,d0")
            ins("lea p10tab,a0")
            ins("move.l (a0,d0.w*4),a0")
            ins("jmp (a0)")
        elif op == 0x11:
            handler(name)
            ins("moveq #0,d0")
            ins("move.b (a4)+,d0")
            ins("lea p11tab,a0")
            ins("move.l (a0,d0.w*4),a0")
            ins("jmp (a0)")
        elif op in (0x12, 0x1b):                      # NOP
            handler(name)
            next_op(2)
        elif op == 0x16:
            gen_lbranch(name, "ra", 0)
        elif op == 0x17:                              # LBSR
            handler(name)
            ins("move.w (a4)+,d0")
            get_pc("d1")
            rel_jump()
            push_word_d1()
            next_op(9)
        elif op == 0x1a:                              # ORCC
            handler(name)
            ins("or.b (a4)+,d3")
            next_op(3)
        elif op == 0x1c:                              # ANDCC
            handler(name)
            ins("and.b (a4)+,d3")
            go_check_ints(3)
        elif op == 0x1d:                              # SEX
            handler(name)
            ins("move.b d5,d1")
            ins("ext.w d1")
            ins("move.w d1,d2")
            ins("lsr.w #8,d2")
            ins("move.b d2,d4")
            ins("tst.w d1")
            flags(0x0c)
            next_op(2)
        elif op in (0x30, 0x31, 0x32, 0x33):          # LEA
            handler(name)
            ea_indexed()
            if op == 0x30:
                ins("move.w d0,d6")
            elif op == 0x31:
                ins("move.w d0,%d(a5)" % O_Y)
            elif op == 0x32:
                ins("move.w d0,%d(a5)" % O_S)
                ins("move.b #1,%d(a5)" % O_LDS)
            else:
                ins("move.w d0,%d(a5)" % O_U)
            if op in (0x30, 0x31):
                ins("tst.w d0")
                flags(0x04)
            next_op(2)
        elif op == 0x34:
            gen_push(op, False)
        elif op == 0x35:
            gen_pull(op, False)
        elif op == 0x36:
            gen_push(op, True)
        elif op == 0x37:
            gen_pull(op, True)
        elif op == 0x39:                              # RTS
            handler(name)
            ins("moveq #0,d0")
            ins("move.w %d(a5),d0" % O_S)
            rd8()
            ins("lsl.w #8,d1")
            ins("addq.w #1,d0")
            rd8()
            ins("addq.w #1,d0")
            ins("move.w d0,%d(a5)" % O_S)
            ins("moveq #0,d0")
            ins("move.w d1,d0")
            set_pc("d0")
            next_op(5)
        elif op == 0x3a:                              # ABX
            handler(name)
            ins("moveq #0,d1")
            ins("move.b d5,d1")
            ins("add.w d1,d6")
            next_op(3)
        elif op == 0x3d:                              # MUL
            handler(name)
            z, c = L(), L()
            ins("moveq #0,d0")
            ins("move.b d4,d0")
            ins("moveq #0,d1")
            ins("move.b d5,d1")
            ins("mulu.w d1,d0")
            ins("move.b d0,d5")
            ins("move.w d0,d1")
            ins("lsr.w #8,d1")
            ins("move.b d1,d4")
            ins("and.b #$fa,d3")
            ins("tst.w d0")
            ins("bne.s " + z)
            ins("or.b #4,d3")
            emit(z + ":")
            ins("btst #7,d0")
            ins("beq.s " + c)
            ins("or.b #1,d3")
            emit(c + ":")
            next_op(11)
        elif op in (0x1e, 0x1f):
            gen_tfr_exg(op)
        elif op == 0x3b:
            gen_rti()
        else:                                         # 13 SYNC, 14 15 18 19 38 3C 3E 3F
            fallback(name)


def gen_page10():
    names = []
    for op in range(256):
        name = "p10_%02X" % op
        names.append(name)
        mode = (op >> 4) & 3
        if (op & 0xf0) == 0x20:
            cc = ["ra", "rn", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"][op & 15]
            if cc == "ra":
                gen_lbranch(name, None, 1)
            else:
                gen_lbranch(name, cc, 1)
        elif op in (0x83, 0x93, 0xa3, 0xb3, 0x8c, 0x9c, 0xac, 0xbc):     # CMPD, CMPY
            handler(name)
            cyc = 2 + operand16(mode)
            if (op & 0x0f) == 3:
                get_d("d0")
                ins("cmp.w d1,d0")
            else:
                ins("move.w %d(a5),d0" % O_Y)
                ins("cmp.w d1,d0")
            flags(0x0f)
            next_op(cyc + 1)
        elif op in (0x8e, 0x9e, 0xae, 0xbe, 0xce, 0xde, 0xee, 0xfe):     # LDY, LDS
            handler(name)
            cyc = 2 + operand16(mode)
            if op >= 0xc0:
                ins("move.w d1,%d(a5)" % O_S)
                ins("move.b #1,%d(a5)" % O_LDS)
                ins("tst.w d1")
            else:
                ins("move.w d1,%d(a5)" % O_Y)
            flags(0x0e)
            next_op(cyc)
        elif op in (0x9f, 0xaf, 0xbf, 0xdf, 0xef, 0xff):                  # STY, STS
            handler(name)
            cyc = 2 + ea_mode(mode)
            ins("move.w %d(a5),d1" % (O_S if op >= 0xc0 else O_Y))
            flags(0x0e)
            wr16()
            next_op(cyc + 2)
        else:
            fallback(name, prefix=True)
    return names


def gen_page11():
    names = []
    for op in range(256):
        name = "p11_%02X" % op
        names.append(name)
        mode = (op >> 4) & 3
        if op in (0x83, 0x93, 0xa3, 0xb3, 0x8c, 0x9c, 0xac, 0xbc):       # CMPU, CMPS
            handler(name)
            cyc = 2 + operand16(mode)
            ins("move.w %d(a5),d0" % (O_U if (op & 0x0f) == 3 else O_S))
            ins("cmp.w d1,d0")
            flags(0x0f)
            next_op(cyc + 1)
        else:
            fallback(name, prefix=True)
    return names


# ---------------------------------------------------------------- frame

def gen_frame():
    emit("""; 6809 core for the 68k - generated by tools/gen_6809_68k.py, do not edit
\tmachine\t68060
\tsection\t"CODE",code
\txref\t_m6809_step
\txref\t_m6809_rd_slow
\txref\t_m6809_wr_slow
\txref\t_m6809_idle_hit

; int32_t m6809_run_asm(M6809 *c, int32_t cycles)
\tpublic\t_m6809_run_asm
\tcnop\t0,4
_m6809_run_asm:
\tmovem.l\td2-d7/a2-a6,-(sp)
\tmove.l\t48(sp),a5
\tmove.l\t52(sp),d0
\tadd.l\td0,%(ICOUNT)d(a5)
\tmove.l\t%(ICOUNT)d(a5),d7
\tmove.l\td7,-(sp)                     ; the start value (0(sp))
\tclr.b\t%(IVALID)d(a5)                 ; idle loops: the world may have changed
\tbsr\tload_regs
\ttst.l\td7
\tble\texit_loop
\tbra\tcheck_ints

; registers from the struct (and the table pointers)
load_regs:
\tlea\t%(RBASE)d(a5),a2
\tlea\t%(WBASE)d(a5),a1
\tlea\toptab,a6
\tmoveq\t#0,d3
\tmove.b\t%(CC)d(a5),d3
\tmove.b\t%(A)d(a5),d4
\tmove.b\t%(B)d(a5),d5
\tmoveq\t#0,d6
\tmove.w\t%(X)d(a5),d6
\tmoveq\t#0,d0
\tmove.w\t%(PC)d(a5),d0
\tmove.w\td0,d2
\tlsr.w\t#8,d2
\tmove.l\t(a2,d2.w*4),a3
\tlea\t(a3,d0.l),a4
\trts

; registers into the struct
store_regs:
\tmove.b\td3,%(CC)d(a5)
\tmove.b\td4,%(A)d(a5)
\tmove.b\td5,%(B)d(a5)
\tmove.w\td6,%(X)d(a5)
\tmove.l\ta4,d0
\tsub.l\ta3,d0
\tmove.w\td0,%(PC)d(a5)
\tmove.l\td7,%(ICOUNT)d(a5)
\trts

; an interrupt pending, CWAI/SYNC waiting or the free run mode: one step of the C core
check_ints:
\ttst.l\td7
\tble\texit_loop
\ttst.b\t%(WAIT)d(a5)
\tbne.s\tc_step
\ttst.b\t%(FREE)d(a5)
\tbne.s\tc_step
\ttst.b\t%(NMIP)d(a5)
\tbne.s\tc_step
\tbtst\t#6,d3
\tbne.s\t.nofirq
\ttst.b\t%(FIRQ)d(a5)
\tbne.s\tc_step
.nofirq:
\tbtst\t#4,d3
\tbne.s\tdispatch
\ttst.b\t%(IRQ)d(a5)
\tbne.s\tc_step
dispatch:
\tmove.l\t%(TRMAP)d(a5),d0
\tbne.s\ttr_look
dispatch_plain:
\tmoveq\t#0,d0
\tmove.b\t(a4)+,d0
\tmove.l\t(a6,d0.w*4),a0
\tjmp\t(a0)

; after a control flow instruction of translated code (a4 = the new PC): its translation if
; there is one (8000-FFFF), else the core
tr_next:
\tmove.l\t%(TRMAP)d(a5),d0
\tbeq.s\tdispatch_plain
tr_look:
\tmove.l\ta4,d1
\tsub.l\ta3,d1
\ttst.w\td1
\tbpl.s\tdispatch_plain
\tand.l\t#$7fff,d1
\tmove.l\td0,a0
\tmove.l\t(a0,d1.l*4),d0
\tbeq.s\tdispatch_plain
\tmove.l\td0,a0
\tjmp\t(a0)

; one instruction (or interrupt entry) by the C core: PC is at the instruction
c_step:
\tbsr\tstore_regs
\tmove.l\t%(TOTAL)d(a5),-(sp)           ; the total is counted at the exit
\tmove.l\ta5,-(sp)
\tjsr\t_m6809_step
\taddq.l\t#4,sp
\tmove.l\t(sp)+,%(TOTAL)d(a5)
\tmove.l\t%(ICOUNT)d(a5),d7
\tbsr\tload_regs
\ttst.l\td7
\tble\texit_loop
\tbra\tcheck_ints

; a backward branch or a jump reached an address in the idle map (PC already set, cycles
; already subtracted): whole turns of the loop may be skipped. The same as idle_check of the
; C core (m6809.c), in assembler: the loop's entry, its code (ROM banks), no interrupt pending,
; the registers equal to the last arrival in this run -> skip (icount - 1) / turn turns; then
; the registers are kept for the next arrival (the snapshot as idle_regs: A B DP CC X Y U S).
idle_hit:
\ttst.l\td7
\tble\texit_loop
\tmove.l\ta4,d0
\tsub.l\ta3,d0
\tlea\t%(IPC)d(a5),a0
\tmoveq\t#0,d1
\tmoveq\t#0,d2
\tmove.b\t%(NIDLE)d(a5),d2
\tbeq\tdispatch
.find:
\tcmp.w\t(a0)+,d0
\tbeq.s\t.found
\taddq.l\t#1,d1
\tsubq.l\t#1,d2
\tbne.s\t.find
\tbra\tdispatch
.found:
\tmove.l\t%(ISIG)d(a5,d1.l*4),a0         ; the loop's code
\tmoveq\t#0,d2
\tmove.b\t%(ILEN)d(a5,d1.l),d2
\tmoveq\t#0,d1
.sig:
\tmove.b\t(a4,d1.l),d0
\tcmp.b\t(a0)+,d0
\tbne\t.badsig
\taddq.l\t#1,d1
\tcmp.l\td2,d1
\tblo.s\t.sig
\ttst.b\t%(WAIT)d(a5)                   ; an interrupt is taken first
\tbne\t.pend
\ttst.b\t%(FREE)d(a5)
\tbne\t.pend
\ttst.b\t%(NMIP)d(a5)
\tbne\t.pend
\tbtst\t#6,d3
\tbne.s\t.nofirq
\ttst.b\t%(FIRQ)d(a5)
\tbne\t.pend
.nofirq:
\tbtst\t#4,d3
\tbne.s\t.noirq
\ttst.b\t%(IRQ)d(a5)
\tbne\t.pend
.noirq:
\tmove.l\ta4,d0
\tsub.l\ta3,d0                          ; PC
\ttst.b\t%(IVALID)d(a5)
\tbeq\t.snap
\tcmp.w\t%(IAT)d(a5),d0
\tbne\t.snap
\tlea\t%(IREGS)d(a5),a0
\tcmp.b\t(a0)+,d4
\tbne\t.snap
\tcmp.b\t(a0)+,d5
\tbne\t.snap
\tmove.b\t%(DP)d(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tcmp.b\t(a0)+,d3
\tbne\t.snap
\tmove.w\td6,d2
\tlsr.w\t#8,d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tcmp.b\t(a0)+,d6
\tbne\t.snap
\tmove.b\t%(Y)d(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tmove.b\t%(Y)d+1(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tmove.b\t%(U)d(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tmove.b\t%(U)d+1(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tmove.b\t%(S)d(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tmove.b\t%(S)d+1(a5),d2
\tcmp.b\t(a0)+,d2
\tbne\t.snap
\tmove.l\t%(IICOUNT)d(a5),d1              ; turn = cycles of one turn
\tsub.l\td7,d1
\tble\t.snap
\tcmp.l\td1,d7
\tble\t.snap
\tmove.l\td7,d2
\tsubq.l\t#1,d2
\tdivu.l\td1,d2                         ; k = (icount - 1) / turn
\tmulu.l\td1,d2
\tsub.l\td2,d7                          ; (the total follows from icount at the exit)
\tadd.l\td2,%(ISKIP)d(a5)
.snap:
\tmove.b\t#1,%(IVALID)d(a5)
\tmove.w\td0,%(IAT)d(a5)
\tmove.l\td7,%(IICOUNT)d(a5)
\tlea\t%(IREGS)d(a5),a0
\tmove.b\td4,(a0)+
\tmove.b\td5,(a0)+
\tmove.b\t%(DP)d(a5),(a0)+
\tmove.b\td3,(a0)+
\tmove.w\td6,d2
\tlsr.w\t#8,d2
\tmove.b\td2,(a0)+
\tmove.b\td6,(a0)+
\tmove.b\t%(Y)d(a5),(a0)+
\tmove.b\t%(Y)d+1(a5),(a0)+
\tmove.b\t%(U)d(a5),(a0)+
\tmove.b\t%(U)d+1(a5),(a0)+
\tmove.b\t%(S)d(a5),(a0)+
\tmove.b\t%(S)d+1(a5),(a0)+
\tbra\tdispatch
.badsig:
\tclr.b\t%(IVALID)d(a5)
\tbra\tdispatch
.pend:
\tclr.b\t%(IVALID)d(a5)
\tbra\tcheck_ints

exit_loop:
\tbsr\tstore_regs
\tmove.l\t(sp)+,d0                     ; start - end = cycles executed
\tsub.l\td7,d0
\tadd.l\td0,%(TOTAL)d(a5)
\tmovem.l\t(sp)+,d2-d7/a2-a6
\trts

; d0.l address -> d1.b (I/O through the C read handler); keeps d0
rd_slow:
\tcmp.w\t12416(a5),d0                  ; fast_addr: its value is kept by the machine
\tbne.s\t.call
\ttst.b\t12419(a5)                     ; fast_ok
\tbeq.s\t.call
\tmove.b\t12418(a5),d1                 ; fast_val
\trts
.call:
\tmovem.l\td0/a2,-(sp)
\tmove.l\td0,-(sp)
\tmove.l\ta5,-(sp)
\tjsr\t_m6809_rd_slow
\taddq.l\t#8,sp
\tmove.b\td0,d1\t\t\t; only the low byte: callers may keep a high byte in d1
\tmovem.l\t(sp)+,d0/a2
\tlea\t%(WBASE)d(a5),a1
\trts

; d0.l address -> d1.w, two byte reads (high byte first); keeps d0
rd16_slow:
	move.l	d0,-(sp)
	bsr	rd8_sub
	move.b	d1,-(sp)
	addq.w	#1,d0
	bsr	rd8_sub
	move.b	(sp)+,d0
	lsl.w	#8,d0
	move.b	d1,d0
	move.w	d0,d1
	move.l	(sp)+,d0
	rts

rd8_sub:
""" % dict(ICOUNT=O_ICOUNT, TOTAL=O_TOTAL, IVALID=O_IVALID, RBASE=RBASE, WBASE=WBASE, CC=O_CC,
           A=O_A, B=O_B, X=O_X, PC=O_PC, WAIT=O_WAIT, FREE=O_FREE, NMIP=O_NMIP, FIRQ=O_FIRQ, IRQ=O_IRQ,
           DP=O_DP, Y=O_Y, U=O_U, S=O_S, TRMAP=O_TRMAP, ISIG=4120, IICOUNT=4152, ISKIP=4156, IPC=4172, IAT=4188, ILEN=4201,
           NIDLE=4209, IREGS=4211))
    rd8()
    ins("rts")
    emit("""
; d0.l address, d1.b data (I/O through the C write handler); keeps d0 d1. The write may switch
; a ROM bank: PC is mapped again.
wr_slow:
\tmove.l\ta4,d2
\tsub.l\ta3,d2
\tmove.w\td2,%(PC)d(a5)
\tmovem.l\td0-d1,-(sp)
\tmoveq\t#0,d2
\tmove.b\td1,d2
\tmove.l\td2,-(sp)
\tmove.l\td0,-(sp)
\tmove.l\ta5,-(sp)
\tjsr\t_m6809_wr_slow
\tlea\t12(sp),sp
\tmovem.l\t(sp)+,d0-d1
\tlea\t%(WBASE)d(a5),a1
\tmoveq\t#0,d2
\tmove.w\t%(PC)d(a5),d2
\tmove.l\td2,a0
\tlsr.w\t#8,d2
\tmove.l\t(a2,d2.w*4),a3
\tmove.l\ta0,d2
\tlea\t(a3,d2.l),a4
\trts

; d0.l address, d1.w data: two byte writes (high byte first); keeps d0 d1
wr16_slow:
\tmove.l\td0,-(sp)
\tmove.w\td1,-(sp)
\tror.w\t#8,d1
\tbsr\twr8_sub
\taddq.w\t#1,d0
\tror.w\t#8,d1
\tbsr\twr8_sub
\tmove.w\t(sp)+,d1
\tmove.l\t(sp)+,d0
\trts

wr8_sub:
""" % dict(PC=O_PC, WBASE=WBASE))
    wr8()
    ins("rts")


def table(name, labels):
    emit("", "\tcnop\t0,4", name)
    for i in range(0, len(labels), 8):
        emit("\tdc.l " + ",".join(labels[i:i + 8]))


TRACE_REC = """
; 16 bytes per instruction: PC.w A.b B.b X.w CC.b 0 icount.l cpu.l
\txref\t_m6809_trace_buf
\txref\t_m6809_trace_pos
trace_rec:
\tmove.w\tccr,-(sp)
\tmovem.l\td0/a0,-(sp)
\tmove.l\t_m6809_trace_pos,d0
\tand.l\t#1023,d0
\tlsl.l\t#4,d0
\tlea\t_m6809_trace_buf,a0
\tadd.l\td0,a0
\tmove.l\ta4,d0
\tsub.l\ta3,d0
\tmove.w\td0,(a0)+
\tmove.b\td4,(a0)+
\tmove.b\td5,(a0)+
\tmove.w\td6,(a0)+
\tmove.b\td3,(a0)+
\tclr.b\t(a0)+
\tmove.l\td7,(a0)+
\tmove.l\ta5,(a0)+
\taddq.l\t#1,_m6809_trace_pos
\tmovem.l\t(sp)+,d0/a0
\tmove.w\t(sp)+,ccr
\trts
"""


INV = {"eq": "ne", "ne": "eq", "cs": "cc", "cc": "cs", "mi": "pl", "pl": "mi", "vs": "vc", "vc": "vs",
       "hi": "ls", "ls": "hi", "ge": "lt", "lt": "ge", "gt": "le", "le": "gt", "hs": "lo", "lo": "hs"}


def gen_templates(core_labels):
    """Templates for the translator (m6809_tr.c): every handler again, with @NEXT instead of the
    dispatch and every reference outside the handler absolute (the code is copied elsewhere).
    Table _m6809_tpl: per page (0, 10, 11) and opcode: start, end, flow (1 = the instruction
    sets PC: the template ends with jmp tr_next); 0, 0 = no template (C core, prefix)."""
    global TPL, lines
    saved, lines, TPL = lines, [], True
    gen_page0()
    gen_page10()
    gen_page11()
    tl, lines, TPL = lines, saved, False
    segs, cur = {}, None
    for l in tl:
        m = re.match(r"^(op_[0-9A-F]{2}|p1[01]_[0-9A-F]{2}):$", l)
        if m:
            cur = m.group(1)
            segs[cur] = []
            continue
        if cur:
            segs[cur].append(l)
    ident = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
    out = ["", "; ---- templates for the translator (m6809_tr.c), generated, see gen_templates", "\tcnop\t0,4"]
    entries = []
    for page, prefix in ((0, "op_"), (1, "p10_"), (2, "p11_")):
        for op in range(256):
            name = "%s%02X" % (prefix, op)
            body = segs.get(name)
            code = [l for l in body if l.strip()] if body else []
            if (not code or "bra c_step" in "\n".join(code) or (page == 0 and op in (0x10, 0x11))):
                entries.append(None)
                continue
            tname = "tp_" + name
            end = tname + "_end"
            local = {l[:-1] for l in code if re.match(r"^[A-Za-z_.][A-Za-z0-9_.]*:$", l)} | {end}
            flow = any(re.search(r",\s*a4\s*$", l.split(";")[0]) for l in code if l.startswith("\t"))
            while code and code[-1] == "\t@NEXT" and not flow:
                code.pop()                          # falls through into the next instruction
            res = []
            for l in code:
                if l == "\t@NEXT":
                    res.append("\tjmp (tr_next).l" if flow else "\tbra.w " + end)
                    continue
                m = re.match(r"^\t(\S+)\s+(.*?)\s*$", l)
                if not m:
                    res.append(l)
                    continue
                mn, opd = m.group(1), m.group(2)
                base = mn.split(".")[0]
                if ident.match(opd) and opd not in local and opd in core_labels:
                    if base in ("bsr", "jsr"):
                        res.append("\tjsr (%s).l" % opd)
                        continue
                    if base in ("bra", "jmp"):
                        res.append("\tjmp (%s).l" % opd)
                        continue
                    if base[0] == "b" and base[1:] in INV:
                        res.append("\tb%s.s *+8" % INV[base[1:]])
                        res.append("\tjmp (%s).l" % opd)
                        continue
                    raise ValueError("%s: %s" % (name, l))
                lm = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)(\(pc\))?,(a[0-6])$", opd)
                if base == "lea" and lm and lm.group(1) not in local:
                    # movea.l #label,An as raw words: vasm would make "move.l #label,An" a
                    # PC-relative lea, which is wrong once the template is copied elsewhere
                    res.append("\tdc.w $%04x" % (0x207c | int(lm.group(3)[1]) << 9))
                    res.append("\tdc.l %s" % lm.group(1))
                    continue
                for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", opd):
                    if tok in core_labels and tok not in local:
                        raise ValueError("%s: external reference %s" % (name, l))
                res.append(l)
            out.append(tname + ":")
            out.extend(res)
            out.append(end + ":")
            entries.append((tname, end, 1 if flow else 0))
    out += ["", "\tcnop\t0,4", "\tpublic\t_m6809_tpl", "_m6809_tpl:"]
    for e in entries:
        out.append("\tdc.l %s,%s,%d" % e if e else "\tdc.l 0,0,0")
    return out, sum(1 for e in entries if e)


def absolutize(name, code, local, core_labels):
    """references outside a template made absolute (see gen_templates)"""
    ident = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
    res = []
    for l in code:
        m = re.match(r"^\t(\S+)\s+(.*?)\s*$", l)
        if not m:
            res.append(l)
            continue
        mn, opd = m.group(1), m.group(2)
        base = mn.split(".")[0]
        if ident.match(opd) and opd not in local and opd in core_labels:
            if base in ("bsr", "jsr"):
                res.append("\tjsr (%s).l" % opd)
                continue
            if base in ("bra", "jmp"):
                res.append("\tjmp (%s).l" % opd)
                continue
            if base[0] == "b" and base[1:] in INV:
                res.append("\tb%s.s *+8" % INV[base[1:]])
                res.append("\tjmp (%s).l" % opd)
                continue
            raise ValueError("%s: %s" % (name, l))
        lm = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)(\(pc\))?,(a[0-6])$", opd)
        if base == "lea" and lm and lm.group(1) not in local:
            res.append("\tdc.w $%04x" % (0x207c | int(lm.group(3)[1]) << 9))
            res.append("\tdc.l %s" % lm.group(1))
            continue
        for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", opd):
            if tok in core_labels and tok not in local:
                raise ValueError("%s: external reference %s" % (name, l))
        res.append(l)
    return res


HOLE_SEQ = [0]


def tpl2_piece(name, code, core_labels):
    """one specialized template (or address piece) from TPL == 2 code: None if it cannot be
    specialized, else (parts, cycles, ea, holes): parts = [pre lines, post lines] (split at @EA),
    holes = [(part, label, word)] in operand order. Rules: one exit (@NEXT at the end or none for
    pieces), no interrupt check, a4 only read as (a4)+ (those become holes with the placeholders
    $A5 / $A5A5), no cycles inside a conditional part."""
    lines = [l for l in code if l.strip()]
    if any(l.strip() in ("@CHECK",) or "c_step" in l for l in lines):
        return None
    nexts = [i for i, l in enumerate(lines) if l == "\t@NEXT"]
    if len(nexts) > 1 or (nexts and nexts[0] != len(lines) - 1):
        return None
    if nexts:
        lines = lines[:-1]
    parts, part, ea, cyc, holes = [[], []], 0, 0, 0, []
    open_br = set()
    for l in lines:
        s = l.strip()
        if s.startswith("@EA"):
            if ea:
                return None
            ea, part = int(s[3]), 1
            continue
        if s.startswith("@CYC"):
            if open_br:
                return None
            cyc += int(s.split()[1])
            continue
        if s.startswith("@"):
            return None
        lab = re.match(r"^([A-Za-z_.][A-Za-z0-9_.]*):$", l)
        if lab:
            open_br.discard(lab.group(1))
            parts[part].append(l)
            continue
        m = re.match(r"^\t(b\w+)(\.[sw])?\s+(L\d+)$", l)
        if m:
            open_br.add(m.group(3))
        src = l.split(";")[0]
        if re.search(r"\ba4\b", src.replace("(a4)+", "")):
            return None
        if "(a4)+" in src:
            m = re.match(r"^\t(\w+)\.([bw])\s+\(a4\)\+,(.*)$", src.strip("\n"))
            if not m or src.count("(a4)+") != 1:
                return None
            HOLE_SEQ[0] += 1
            hl = "H%d" % HOLE_SEQ[0]
            word = m.group(2) == "w"
            parts[part].append(hl + ":")
            parts[part].append("\t%s.%s #$%s,%s" % (m.group(1), m.group(2), "a5a5" if word else "a5", m.group(3)))
            holes.append((part, hl, word))
            continue
        parts[part].append(l)
    if len(holes) > 2:
        return None
    local = set()
    for p in parts:
        local |= {l[:-1] for l in p if re.match(r"^[A-Za-z_.][A-Za-z0-9_.]*:$", l)}
    try:
        parts = [absolutize(name, p, local, core_labels) for p in parts]
    except ValueError:
        return None
    return parts, cyc, ea, holes


def gen_templates2(core_labels):
    """Specialized templates for the translator (m6809_tr.c, translation 2): the handlers without
    reading operands from the code (holes for the translator), the address computation as a
    separate piece (direct, extended, one per indexed postbyte), the cycles counted by the
    translator. Tables:
      _m6809_tq   768 rows (page 0, 10, 11) of: pre start, pre end, post start, post end,
                  info (cycles | ea << 8 | holes << 12; ea 1 direct, 2 indexed, 3 extended),
                  hole words (2 x: bit 15 post part, bit 14 word, offset of the instruction)
      _m6809_tqea rows: direct, extended, indexed 00-FF: start, end, info (cycles | holes << 12),
                  hole words; start 0 = not possible."""
    global TPL, lines
    saved, lines, TPL = lines, [], 2
    gen_page0()
    gen_page10()
    gen_page11()
    tl = lines
    pieces = {}
    lines = []
    for post in range(256):
        emit("ea_idx_%02X:" % post)
        idx_body(post)
    il = lines
    lines, TPL = saved, False
    segs, cur = {}, None
    for l in tl + il:
        m = re.match(r"^(op_[0-9A-F]{2}|p1[01]_[0-9A-F]{2}|ea_idx_[0-9A-F]{2}):$", l)
        if m:
            cur = m.group(1)
            segs[cur] = []
            continue
        if cur:
            segs[cur].append(l)
    out = ["", "; ---- specialized templates (translation 2, m6809_tr.c), see gen_templates2", "\tcnop\t0,4"]
    rows, n = [], 0

    def hole_words(holes, starts):
        ws = []
        for part, hl, word in holes:
            ws.append("%s-%s+%d" % (hl, starts[part], (0x8000 if part else 0) + (0x4000 if word else 0)))
        while len(ws) < 2:
            ws.append("0")
        return ws

    for page, prefix in ((0, "op_"), (1, "p10_"), (2, "p11_")):
        for op in range(256):
            name = "%s%02X" % (prefix, op)
            r = tpl2_piece(name, segs.get(name, []), core_labels) if segs.get(name) else None
            if r is None or (page == 0 and op in (0x10, 0x11)) or not (r[0][0] or r[0][1]) and not r[1]:
                rows.append(None)
                continue
            parts, cyc, ea, holes = r
            t = "tq_" + name
            starts = [t + "_a", t + "_b"]
            out.append(starts[0] + ":")
            out.extend(parts[0])
            out.append(starts[1] + ":")
            out.extend(parts[1])
            out.append(t + "_e:")
            rows.append("\tdc.l %s,%s,%s,%s,%d\n\tdc.w %s" % (starts[0], starts[1], starts[1], t + "_e",
                        cyc | ea << 8 | len(holes) << 12, ",".join(hole_words(holes, starts))))
            n += 1
    out += ["", "\tcnop\t0,4", "\tpublic\t_m6809_tq", "_m6809_tq:"]
    for r in rows:
        out.append(r if r else "\tdc.l 0,0,0,0,0\n\tdc.w 0,0")
    # address pieces
    ea_rows = []
    direct = ["\tmoveq #0,d0", "\tmove.w %d(a5),d0" % O_DP, "Hd:", "\tmove.b #$a5,d0"]
    ext = ["\tmoveq #0,d0", "Hx:", "\tmove.w #$a5a5,d0"]
    out += ["", "tqd_s:"] + direct + ["tqd_e:", "tqx_s:"] + ext + ["tqx_e:"]
    ea_rows.append("\tdc.l tqd_s,tqd_e,%d\n\tdc.w Hd-tqd_s,0" % (2 | 1 << 12))
    ea_rows.append("\tdc.l tqx_s,tqx_e,%d\n\tdc.w Hx-tqx_s+$4000,0" % (3 | 1 << 12))
    ni = 0
    for post in range(256):
        name = "ea_idx_%02X" % post
        r = tpl2_piece(name, segs.get(name, []), core_labels)
        mode = post & 0x0f
        if r is None or r[2] or (post & 0x80 and mode in (0x07, 0x0a, 0x0e)):
            ea_rows.append("\tdc.l 0,0,0\n\tdc.w 0,0")
            continue
        parts, cyc, _, holes = r
        s, e = "tqi_%02X_s" % post, "tqi_%02X_e" % post
        out.append(s + ":")
        out.extend(parts[0])
        out.append(e + ":")
        ws = hole_words([(0, hl, w) for (_, hl, w) in holes], [s, s])
        ea_rows.append("\tdc.l %s,%s,%d\n\tdc.w %s" % (s, e, cyc | len(holes) << 12, ",".join(ws)))
        ni += 1
    out += ["", "\tcnop\t0,4", "\tpublic\t_m6809_tqea", "_m6809_tqea:"] + ea_rows
    return out, n, ni


def main():
    gen_frame()
    gen_tfr_helpers()
    gen_page0()
    p10 = gen_page10()
    p11 = gen_page11()
    for post in range(256):
        gen_idx(post)
    table("optab", ["op_%02X" % i for i in range(256)])
    table("p10tab", p10)
    table("p11tab", p11)
    table("idxtab", ["idx_%02X" % i for i in range(256)])
    core_labels = {l[:-1] for l in "\n".join(lines).split("\n") if re.match(r"^[A-Za-z_][A-Za-z0-9_]*:$", l)}
    tpl, ntpl = gen_templates(core_labels)
    lines.extend(tpl)
    print("templates: %d" % ntpl)
    tpl2, ntq, nqi = gen_templates2(core_labels)
    lines.extend(tpl2)
    print("specialized templates: %d, indexed address pieces: %d" % (ntq, nqi))
    text = "\n".join(lines) + "\n"
    # entry points of the translated code for m6809_tr.c
    for lab in ("tr_next", "dispatch_plain", "exit_loop", "c_step"):
        text = text.replace("\n%s:\n" % lab, "\n\tpublic\t_m6809_%s\n_m6809_%s:\n%s:\n" % (lab, lab, lab), 1)
    if TRACE:
        text = text.replace("\ndispatch:\n", "\ndispatch:\n\tbsr\ttrace_rec\n", 1)
        text += TRACE_REC
    with open(OUT, "w", newline="\n") as f:
        f.write(text)
    print("%s: %d lines" % (OUT, len(lines)))


if __name__ == "__main__":
    main()
