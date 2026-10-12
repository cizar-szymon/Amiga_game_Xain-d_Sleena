"""Translate the main CPU's sprite list routine (DCEC-DE88: DCEC goes through 16 priorities x the
object records, DD48 turns an object into hardware sprite entries at 0100) into C, one case per
instruction with its operands and addresses resolved (port/src/m6809_spr.c, a unit of its own: as
a part of m6809.c, vbcc -O2/-O3 took minutes with the helpers inlined 200 times).

    python tools/gen_sprites_c.py ROM/xsleena/p9-08.ic66 tools/sprites_cycles.txt port/src/m6809_spr.c port/src/m6809.c

Exact by construction: every instruction does what the C core does (the same helpers, copied: f8/f16,
push_regs, pull_regs, tfr_read/tfr_write, cond, gosub, RD/WR), then icount and total get the
cycles the C core counts for it (measured: tools/spritecyc.c, "pc next cycles count";
they do not vary). The routine stops at every instruction boundary when the slice is used up,
so interrupts and the other CPUs interleave as before. Instructions without a measured cycle
count, writes to pages without memory (I/O) and anything outside the range go back to the core.
"""
import sys

START, END = 0xDCEC, 0xDE89

rom = open(sys.argv[1], "rb").read()
cyc = {}
for line in open(sys.argv[2]):
    f = line.split()
    if len(f) == 4 and not f[0].startswith("VARIES"):
        cyc[(int(f[0], 16), int(f[1], 16))] = int(f[2])


def b(a):
    return rom[a - 0x8000]


def w(a):
    return b(a) << 8 | b(a + 1)


def s8(v):
    return v - 256 if v >= 128 else v


REG16 = {0: "c->x", 1: "c->y", 2: "c->u", 3: "c->s"}


def indexed(a):
    """postbyte at a: (C statements, C expression of the EA, bytes after the postbyte)"""
    pb = b(a)
    r = REG16[(pb >> 5) & 3]
    if not pb & 0x80:
        off = pb & 0x1f
        off = off - 32 if off & 0x10 else off
        return "", "(uint16_t)(%s + %d)" % (r, off), 0
    if pb & 0x10:
        raise ValueError("indirect at %04X" % a)
    m = pb & 0x0f
    if m == 0x00:
        return "ea = %s; %s += 1;" % (r, r), "ea", 0
    if m == 0x01:
        return "ea = %s; %s += 2;" % (r, r), "ea", 0
    if m == 0x02:
        return "%s -= 1; ea = %s;" % (r, r), "ea", 0
    if m == 0x03:
        return "%s -= 2; ea = %s;" % (r, r), "ea", 0
    # (the write check uses check_ea(): the same address without changing the register)
    if m == 0x04:
        return "", r, 0
    if m == 0x05:
        return "", "(uint16_t)(%s + (int8_t)c->b)" % r, 0
    if m == 0x06:
        return "", "(uint16_t)(%s + (int8_t)c->a)" % r, 0
    if m == 0x08:
        return "", "(uint16_t)(%s + %d)" % (r, s8(b(a + 1))), 1
    if m == 0x09:
        return "", "(uint16_t)(%s + 0x%04X)" % (r, w(a + 1)), 2
    if m == 0x0b:
        return "", "(uint16_t)(%s + GETD())" % r, 0
    raise ValueError("indexed mode %02X at %04X" % (pb, a))


def check_ea(a, mode):
    """the EA of an operand as an expression without side effects (for the write check)"""
    if mode != 2:
        return operand(a, mode)[1]
    pb = b(a)
    r = REG16[(pb >> 5) & 3]
    if pb & 0x80 and not pb & 0x10 and (pb & 0x0f) in (0, 1, 2, 3):
        return {0: r, 1: r, 2: "(uint16_t)(%s - 1)" % r, 3: "(uint16_t)(%s - 2)" % r}[pb & 0x0f]
    return indexed(a)[1]


def operand(a, mode):
    """mode 0 imm, 1 direct, 2 indexed, 3 extended: (pre statements, ea expr or None, imm, length)"""
    if mode == 1:
        return "", "(uint16_t)(c->dp << 8 | 0x%02X)" % b(a), None, 1
    if mode == 2:
        pre, ea, n = indexed(a)
        return pre, ea, None, 1 + n
    if mode == 3:
        return "", "0x%04X" % w(a), None, 2
    raise ValueError


ALU8 = {0x0: "sub", 0x1: "cmp", 0x4: "and", 0x5: "bit", 0x6: "ld", 0x7: "st", 0x8: "eor", 0xb: "add", 0xa: "or"}


def alu8(kind, reg, m):
    if kind == "sub":
        return "%s = f8(c, CC_NZVC, %s, %s, (uint32_t)((int32_t)%s - %s));" % (reg, reg, m, reg, m)
    if kind == "cmp":
        return "f8(c, CC_NZVC, %s, %s, (uint32_t)((int32_t)%s - %s));" % (reg, m, reg, m)
    if kind == "and":
        return "c->cc &= (uint8_t)~CC_V; %s = f8(c, CC_NZ, 0, %s, (uint8_t)(%s & %s));" % (reg, reg, reg, m)
    if kind == "bit":
        return "c->cc &= (uint8_t)~CC_V; f8(c, CC_NZ, 0, %s, (uint8_t)(%s & %s));" % (reg, reg, m)
    if kind == "eor":
        return "c->cc &= (uint8_t)~CC_V; %s = f8(c, CC_NZ, 0, %s, (uint8_t)(%s ^ %s));" % (reg, reg, reg, m)
    if kind == "or":
        return "c->cc &= (uint8_t)~CC_V; %s = f8(c, CC_NZ, 0, %s, (uint8_t)(%s | %s));" % (reg, reg, reg, m)
    if kind == "add":
        return "%s = f8(c, CC_HNZVC, %s, %s, (uint32_t)%s + %s);" % (reg, reg, m, reg, m)
    if kind == "ld":
        return "%s = %s; NZV8(%s);" % (reg, m, reg)
    raise ValueError(kind)


RMW = {0x0: "neg", 0x3: "com", 0x4: "lsr", 0x8: "asl", 0x9: "rol", 0xa: "dec", 0xc: "inc", 0xd: "tst", 0xf: "clr"}


def rmw(kind, t):
    """C code with the operand in t, the result into r (None: TST, nothing written)"""
    if kind == "neg":
        return "r = f8(c, CC_NZVC, 0, %s, (uint32_t)(-(int32_t)%s));" % (t, t)
    if kind == "com":
        return "c->cc &= (uint8_t)~CC_V; c->cc |= CC_C; r = f8(c, CC_NZ, 0, (uint8_t)~%s, (uint8_t)~%s);" % (t, t)
    if kind == "lsr":
        return "c->cc = (uint8_t)((c->cc & ~CC_C) | (%s & 1)); r = f8(c, CC_NZ, 0, (uint8_t)(%s >> 1), (uint8_t)(%s >> 1));" % (t, t, t)
    if kind == "asl":
        return "r = f8(c, CC_NZVC, %s, %s, (uint32_t)%s << 1);" % (t, t, t)
    if kind == "rol":
        return ("{ uint32_t v = ((uint32_t)%s << 1) | ((c->cc & CC_C) ? 1u : 0u); "
                "c->cc = (uint8_t)((c->cc & ~CC_C) | ((%s & 0x80) ? CC_C : 0)); r = f8(c, CC_NZV, %s, %s, v); }" % (t, t, t, t))
    if kind == "dec":
        return "r = f8(c, CC_NZV, %s, 1, (uint32_t)((int32_t)%s - 1));" % (t, t)
    if kind == "inc":
        return "r = f8(c, CC_NZV, %s, 1, (uint32_t)%s + 1);" % (t, t)
    if kind == "tst":
        return "NZV8(%s);" % t
    if kind == "clr":
        return "c->cc &= (uint8_t)~CC_NZVC; c->cc |= CC_Z; r = 0;"
    raise ValueError(kind)


def gen(pc):
    """(C body, length, list of possible next addresses as (expr or int); writes: list of ea exprs)"""
    op = b(pc)
    a = pc + 1
    page = 0
    if op in (0x10, 0x11):
        page = op
        op = b(a)
        a += 1
    hi, lo = op >> 4, op & 0x0f
    body, nxt, wr = [], None, []          # wr: write addresses (checked before the instruction)

    if page == 0x10 and hi == 0x2:                     # LBcc
        tgt = (a + 2 + w(a)) & 0xffff
        n = a + 2
        return ("if (cond(c, 0x%02X)) { c->pc = 0x%04X; CYC(%%s); } else { c->pc = 0x%04X; CYC(%%s); }" % (op, tgt, n),
                n - pc, [(tgt, "t"), (n, "n")], [])
    if page == 0x10 and op in (0x8e, 0xae):            # LDY
        mode = (op >> 4) & 3
        if mode == 0:
            body.append("c->y = 0x%04X; NZV16(c->y);" % w(a))
            n = a + 2
        else:
            pre, ea, _, ln = operand(a, 2)
            body.append("%s { uint16_t e = %s; c->y = (uint16_t)(RD(c, e) << 8); c->y |= RD(c, (uint16_t)(e + 1)); NZV16(c->y); }" % (pre, ea))
            n = a + ln
        return " ".join(body) + " c->pc = 0x%04X; CYC(%%s);" % n, n - pc, [(n, "n")], []
    if page:
        raise ValueError("page %02X op %02X at %04X" % (page, op, pc))

    if hi == 0x2:                                      # Bcc rel8
        tgt = (a + 1 + s8(b(a))) & 0xffff
        n = a + 1
        if op == 0x20:
            return "c->pc = 0x%04X; CYC(%%s);" % tgt, n - pc, [(tgt, "t")], []
        return ("if (cond(c, 0x%02X)) { c->pc = 0x%04X; CYC(%%s); } else { c->pc = 0x%04X; CYC(%%s); }" % (op, tgt, n),
                n - pc, [(tgt, "t"), (n, "n")], [])
    if op >= 0x80:
        mode = hi & 3
        bside = hi >= 0xc
        reg = "c->b" if bside else "c->a"
        if lo == 0x3:                                  # SUBD / ADDD
            if mode == 0:
                m, pre, ln = "0x%04X" % w(a), "", 2
            else:
                pre, ea, _, ln = operand(a, mode)
                m = "(uint16_t)(RD(c, e) << 8 | RD(c, (uint16_t)(e + 1)))"
                pre = pre + " uint16_t e = %s;" % ea
            body.append("{ %s uint16_t mm = %s; uint16_t d = GETD(); SETD(f16(c, CC_NZVC, d, mm, (uint32_t)d %s mm)); }"
                        % (pre, m, "+" if bside else "-"))
            n = a + ln
        elif lo == 0xc and bside:                      # LDD
            if mode == 0:
                body.append("SETD(0x%04X); NZV16(GETD());" % w(a))
                ln = 2
            else:
                pre, ea, _, ln = operand(a, mode)
                body.append("{ %s uint16_t e = %s; uint8_t h = RD(c, e); uint16_t v = (uint16_t)(h << 8 | RD(c, (uint16_t)(e + 1))); SETD(v); NZV16(v); }" % (pre, ea))
            n = a + ln
        elif lo == 0xd and bside:                      # STD
            pre, ea, _, ln = operand(a, mode)
            wr.append((check_ea(a, mode), ""))
            body.append("{ %s uint16_t e = %s; uint16_t v = GETD(); WR(c, e, (uint8_t)(v >> 8)); WR(c, (uint16_t)(e + 1), (uint8_t)v); NZV16(v); }" % (pre, ea))
            n = a + ln
        elif lo == 0xd and not bside and mode == 3:    # JSR ext
            tgt = w(a)
            n = a + 2
            wr.append(("(uint16_t)(c->s - 1)", ""))
            wr.append(("(uint16_t)(c->s - 2)", ""))
            return "c->pc = 0x%04X; gosub(c, 0x%04X); CYC(%%s);" % (n, tgt), n - pc, [(tgt, "t")], wr
        elif lo == 0xe:                                # LDX / LDU
            r = "c->u" if bside else "c->x"
            if mode == 0:
                body.append("%s = 0x%04X; NZV16(%s);" % (r, w(a), r))
                ln = 2
            else:
                pre, ea, _, ln = operand(a, mode)
                body.append("{ %s uint16_t e = %s; uint8_t h = RD(c, e); %s = (uint16_t)(h << 8 | RD(c, (uint16_t)(e + 1))); NZV16(%s); }" % (pre, ea, r, r))
            n = a + ln
        elif lo == 0xf:                                # STX / STU
            r = "c->u" if bside else "c->x"
            pre, ea, _, ln = operand(a, mode)
            wr.append((check_ea(a, mode), ""))
            body.append("{ %s uint16_t e = %s; uint16_t v = %s; WR(c, e, (uint8_t)(v >> 8)); WR(c, (uint16_t)(e + 1), (uint8_t)v); NZV16(v); }" % (pre, ea, r))
            n = a + ln
        elif lo in ALU8:
            kind = ALU8[lo]
            if kind == "st":
                pre, ea, _, ln = operand(a, mode)
                wr.append((check_ea(a, mode), ""))
                body.append("{ %s uint16_t e = %s; WR(c, e, NZV8(%s)); }" % (pre, ea, reg))
            elif mode == 0:
                body.append(alu8(kind, reg, "0x%02X" % b(a)))
                ln = 1
            else:
                pre, ea, _, ln = operand(a, mode)
                body.append("{ %s uint8_t m = RD(c, %s); %s }" % (pre, ea, alu8(kind, reg, "m")))
            n = a + ln
        else:
            raise ValueError("op %02X at %04X" % (op, pc))
        return " ".join(body) + " c->pc = 0x%04X; CYC(%%s);" % n, n - pc, [(n, "n")], wr

    if hi in (0x0, 0x6, 0x7):                          # read-modify-write in memory
        kind = RMW[lo]
        mode = {0x0: 1, 0x6: 2, 0x7: 3}[hi]
        pre, ea, _, ln = operand(a, mode)
        if kind != "tst":
            wr.append((check_ea(a, mode), ""))
        code = rmw(kind, "t")
        store = "" if kind == "tst" else " WR(c, e, r);"
        body.append("{ %s uint16_t e = %s; uint8_t t = RD(c, e), r; (void)r; (void)t; %s%s }" % (pre, ea, code, store))
        n = a + ln
        return " ".join(body) + " c->pc = 0x%04X; CYC(%%s);" % n, n - pc, [(n, "n")], wr
    if hi in (0x4, 0x5):                               # on A / B
        reg = "c->a" if hi == 4 else "c->b"
        kind = RMW[lo]
        code = rmw(kind, "t")
        store = "" if kind == "tst" else " %s = r;" % reg
        body.append("{ uint8_t t = %s, r; (void)r; (void)t; %s%s }" % (reg, code, store))
        n = a
        return " ".join(body) + " c->pc = 0x%04X; CYC(%%s);" % n, n - pc, [(n, "n")], []
    if op in (0x30, 0x31, 0x32, 0x33):                 # LEA
        pre, ea, n_, = indexed(a)[0], indexed(a)[1], indexed(a)[2]
        r = {0x30: "c->x", 0x31: "c->y", 0x32: "c->s", 0x33: "c->u"}[op]
        if op in (0x30, 0x31):
            body.append("{ %s uint16_t e = %s; f16(c, CC_Z, 0, e, e); %s = e; }" % (pre, ea, r))
        else:
            body.append("{ %s uint16_t e = %s; %s = e;%s }" % (pre, ea, r, " c->lds_seen = 1;" if op == 0x32 else ""))
        n = a + 1 + n_
        return " ".join(body) + " c->pc = 0x%04X; CYC(%%s);" % n, n - pc, [(n, "n")], []
    if op == 0x34:                                     # PSHS
        m = b(a)
        cnt = sum(2 if (m >> i) & 1 and i >= 4 else 1 for i in range(8) if (m >> i) & 1)
        wr.append(("(uint16_t)(c->s - 1)", ""))
        wr.append(("(uint16_t)(c->s - %d)" % cnt, ""))
        n = a + 1
        return "(void)RD(c, c->s); c->pc = 0x%04X; push_regs(c, &c->s, 0x%02X, 0); CYC(%%s);" % (n, m), n - pc, [(n, "n")], wr
    if op == 0x35:                                     # PULS
        m = b(a)
        n = a + 1
        if m & 0x80:
            return "pull_regs(c, &c->s, 0x%02X, 0); CYC(%%s);" % m, n - pc, [(None, "ret")], []
        return "pull_regs(c, &c->s, 0x%02X, 0); c->pc = 0x%04X; CYC(%%s);" % (m, n), n - pc, [(n, "n")], []
    if op == 0x39:                                     # RTS
        return "pull_regs(c, &c->s, 0x80, 0); CYC(%s);", 1, [(None, "ret")], []
    if op == 0x1f:                                     # TFR
        p = b(a)
        n = a + 1
        return ("tfr_write(c, 0x%02X, tfr_read(c, 0x%X, 0));%s c->pc = 0x%04X; CYC(%%s);"
                % (p, p >> 4, " c->lds_seen = 1;" if (p & 0x0f) == 4 else "", n), n - pc, [(n, "n")], [])
    if op == 0x1e:                                     # EXG
        p = b(a)
        n = a + 1
        e168 = 0 if p & 0x80 else 1
        return ("{ uint16_t r1 = tfr_read(c, 0x%X, %d), r2 = tfr_read(c, 0x%02X, %d); tfr_write(c, 0x%02X, r1); "
                "tfr_write(c, 0x%X, r2); } c->pc = 0x%04X; CYC(%%s);" % (p >> 4, e168, p, e168, p, p >> 4, n),
                n - pc, [(n, "n")], [])
    if op == 0x1d:                                     # SEX
        return ("{ uint16_t v = (uint16_t)(int16_t)(int8_t)c->b; SETD(f16(c, CC_NZ, 0, v, v)); } c->pc = 0x%04X; CYC(%%s);"
                % (pc + 1), 1, [(pc + 1, "n")], [])
    if op == 0x3d:                                     # MUL
        return ("{ uint16_t r = (uint16_t)(c->a * c->b); SETD(f16(c, CC_Z, 0, r, r)); if (r & 0x80) c->cc |= CC_C; "
                "else c->cc &= (uint8_t)~CC_C; } c->pc = 0x%04X; CYC(%%s);" % (pc + 1), 1, [(pc + 1, "n")], [])
    raise ValueError("op %02X at %04X" % (op, pc))


# the helpers, copied from the C core so that they stay the same (m6809.c, argument 4)
core = open(sys.argv[4]).read()


NL = chr(10)


def take(start, end_marker=NL + "}" + NL):
    i = core.index(start)
    j = core.index(end_marker, i) + len(end_marker)
    return core[i:j]


prelude = core[core.index("typedef M6809 C6809;"):core.index("static uint8_t RD(C6809")]
helpers = [take("static uint8_t RD(C6809"), take("static void WR(C6809"),
           take("static uint8_t f8(C6809"), take("static uint16_t f16(C6809"),
           take("static uint16_t tfr_read(C6809"), take("static void tfr_write(C6809"),
           take("static void push_regs(C6809"), take("static uint16_t pull16(C6809"),
           take("static void pull_regs(C6809"), take("static void gosub(C6809"),
           take("static int cond(C6809")]
out = []
out.append(NL.join([
    "/* Generated by tools/gen_sprites_c.py - do not edit. The main CPU's sprite list routine",
    "   DCEC-DE88 translated to C, one case per instruction (see the generator). The helpers",
    "   are copied from m6809.c. */",
    '#include "m6809.h"', "", ""]))
out.append(prelude)
out.append(NL.join(["#define NZV8(v) f8(c, CC_NZV, 0, (v), (v))",
                    "#define NZV16(v) f16(c, CC_NZV, 0, (v), (v))", "", ""]))
out.append(NL.join(helpers) + NL)
out.append(NL.join([
    "#define CYC(n) do { c->icount = ic - (n); c->total += (uint32_t)(n); } while (0)",
    "/* a write address without memory (I/O): the core does the instruction */",
    "#define WRCHK(e) if (!c->wpage[(uint16_t)(e) >> 8]) return 0",
    "", ""]))
GROUP = 24                # instructions per function (vbcc -O2/-O3 is very slow on big functions)
cases = []                # (pc, C line or None)
pc = START
count = skipped = 0
while pc < END:
    try:
        body, ln, nexts, wr = gen(pc)
    except ValueError as e:
        sys.exit("cannot translate: %s" % e)
    cy = []
    ok = True
    for nx, kind in nexts:
        if nx is None:
            vals = {v for (p, q), v in cyc.items() if p == pc}
        else:
            vals = {cyc[(pc, nx)]} if (pc, nx) in cyc else set()
        if len(vals) != 1:
            ok = False
            break
        cy.append(vals.pop())
    if not ok and len(nexts) == 2 and sum((pc, nx) in cyc for nx, _ in nexts) == 1:
        # a conditional branch seen one way only: the other way as the C core counts it
        # (Bcc: the same; LBcc: one cycle more when taken)
        lng = b(pc) == 0x10
        (t, _), (n, _) = nexts
        if (pc, t) in cyc:
            cy = [cyc[(pc, t)], cyc[(pc, t)] - (1 if lng else 0)]
        else:
            cy = [cyc[(pc, n)] + (1 if lng else 0), cyc[(pc, n)]]
        ok = True
    if ok:
        checks = "".join("WRCHK(%s); " % e for e, _ in wr)
        code = body % tuple(cy)
        cases.append((pc, "        case 0x%04X: %s%s break;" % (pc, checks, code)))
        count += 1
    else:
        skipped += 1
        cases.append((pc, "        /* %04X: no measured cycles - the core does it */" % pc))
    pc += ln

# groups of GROUP instructions, one function each: returns 0 to give the rest back to the core
# (I/O write, an instruction without cycles in its range), 1 when PC left its range
groups = [cases[i:i + GROUP] for i in range(0, len(cases), GROUP)]
bounds = []
for k, g in enumerate(groups):
    lo = g[0][0]
    hi = groups[k + 1][0][0] if k + 1 < len(groups) else END
    bounds.append((lo, hi))
    out.append(NL.join([
        "static int spr_%d(C6809 *c)    /* %04X-%04X */" % (k, lo, hi - 1),
        "{",
        "    while (c->icount > 0) {",
        "        int32_t ic = c->icount;",
        "        uint16_t ea;",
        "        (void)ea;",
        "        c->ppc = c->pc;",
        "        switch (c->pc) {"] + [line for _, line in g] + [
        "        default: return c->pc < 0x%04X || c->pc >= 0x%04X;" % (lo, hi),
        "        }",
        "    }",
        "    return 0;",
        "}", "", ""]))
out.append(NL.join(["void m6809_run_spr(C6809 *c)", "{", "    for (;;) {", "        uint16_t pc = c->pc;",
                    "        int r;", "        if (c->icount <= 0 || pc < 0x%04X || pc >= 0x%04X)" % (START, END),
                    "            return;"]
                   + ["        %sif (pc < 0x%04X) r = spr_%d(c);" % ("" if k == 0 else "else ", hi, k)
                      for k, (lo, hi) in enumerate(bounds[:-1])]
                   + ["        else r = spr_%d(c);" % (len(bounds) - 1),
                      "        if (!r)", "            return;", "    }", "}", ""]))
open(sys.argv[3], "w", newline="\n").write("".join(out))
print("%d instructions translated, %d left to the core" % (count, skipped))
