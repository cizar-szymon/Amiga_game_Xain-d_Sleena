/* Motorola 6809 emulator, see m6809.h.
 * Cycle counts follow MAME 0.289 (doc/mame0289/m6809.lst, base6x09.lst): one cycle per memory
 * access, opcode/argument fetch and dummy cycle ("vma"). The order of data reads and writes is
 * the same as in MAME, so tests can replay MAME memory logs.
 */
#include "m6809.h"
#include <string.h>

typedef M6809 C6809;

#define CC_C M6809_CC_C
#define CC_V M6809_CC_V
#define CC_Z M6809_CC_Z
#define CC_N M6809_CC_N
#define CC_I M6809_CC_I
#define CC_H M6809_CC_H
#define CC_F M6809_CC_F
#define CC_E M6809_CC_E
#define CC_NZ (CC_N | CC_Z)
#define CC_NZV (CC_N | CC_Z | CC_V)
#define CC_NZVC (CC_N | CC_Z | CC_V | CC_C)
#define CC_HNZVC (CC_H | CC_N | CC_Z | CC_V | CC_C)

#define VEC_SWI3 0xFFF2
#define VEC_SWI2 0xFFF4
#define VEC_FIRQ 0xFFF6
#define VEC_IRQ 0xFFF8
#define VEC_SWI 0xFFFA
#define VEC_NMI 0xFFFC
#define VEC_RESET 0xFFFE

#define EAT(n) (c->icount -= (n))
#define GETD() ((uint16_t)((c->a << 8) | c->b))
#define SETD(v) do { uint16_t d_ = (uint16_t)(v); c->a = (uint8_t)(d_ >> 8); c->b = (uint8_t)d_; } while (0)

static uint8_t RD(C6809 *c, uint16_t a)
{
    const uint8_t *p = c->rpage[a >> 8];
    c->icount--;
    return p ? p[a & 0xff] : c->read(c->ctx, a);
}

static void WR(C6809 *c, uint16_t a, uint8_t v)
{
    uint8_t *p = c->wpage[a >> 8];
    c->icount--;
    if (p)
        p[a & 0xff] = v;
    else
        c->write(c->ctx, a, v);
}

/* opcode and argument fetch (not a data read: tests serve it from ROM) */
static uint8_t FETCH(C6809 *c)
{
    uint16_t a = c->pc++;
    const uint8_t *p = c->rpage[a >> 8];
    c->icount--;
    return p ? p[a & 0xff] : c->read(c->ctx, a);
}

static uint16_t FETCH16(C6809 *c)
{
    uint8_t h = FETCH(c);
    return (uint16_t)((h << 8) | FETCH(c));
}

/* ---- flags (MAME set_flags) ---- */

static uint8_t f8(C6809 *c, uint8_t mask, uint32_t a, uint32_t b, uint32_t r)
{
    c->cc &= (uint8_t)~mask;
    if (mask & CC_H) c->cc |= ((a ^ b ^ r) & 0x10) ? CC_H : 0;
    if (mask & CC_N) c->cc |= (r & 0x80) ? CC_N : 0;
    if (mask & CC_Z) c->cc |= ((uint8_t)r == 0) ? CC_Z : 0;
    if (mask & CC_V) c->cc |= ((a ^ b ^ r ^ (r >> 1)) & 0x80) ? CC_V : 0;
    if (mask & CC_C) c->cc |= (r & 0x100) ? CC_C : 0;
    return (uint8_t)r;
}

static uint16_t f16(C6809 *c, uint8_t mask, uint32_t a, uint32_t b, uint32_t r)
{
    c->cc &= (uint8_t)~mask;
    if (mask & CC_N) c->cc |= (r & 0x8000) ? CC_N : 0;
    if (mask & CC_Z) c->cc |= ((uint16_t)r == 0) ? CC_Z : 0;
    if (mask & CC_V) c->cc |= ((a ^ b ^ r ^ (r >> 1)) & 0x8000) ? CC_V : 0;
    if (mask & CC_C) c->cc |= (r & 0x10000) ? CC_C : 0;
    return (uint16_t)r;
}

#define NZV8(v) f8(c, CC_NZV, 0, (v), (v))
#define NZV16(v) f16(c, CC_NZV, 0, (v), (v))

/* ---- addressing ---- */

static uint16_t ea_direct(C6809 *c)
{
    uint16_t ea = (uint16_t)((c->dp << 8) | FETCH(c));
    EAT(1);
    return ea;
}

static uint16_t ea_extended(C6809 *c)
{
    uint16_t ea = FETCH16(c);
    EAT(1);
    return ea;
}

static uint16_t *ireg(C6809 *c, uint8_t post)
{
    switch (post & 0x60) {
    case 0x00: return &c->x;
    case 0x20: return &c->y;
    case 0x40: return &c->u;
    default: return &c->s;
    }
}

static uint16_t ea_indexed(C6809 *c)
{
    uint8_t post = FETCH(c);
    uint16_t *r = ireg(c, post);
    uint16_t t;

    if (!(post & 0x80)) {
        /* 5-bit offset */
        t = (uint16_t)(*r + (int8_t)((post & 0x0f) | ((post & 0x10) ? 0xf0 : 0x00)));
        EAT(2);
        return t;
    }
    switch (post & 0x0f) {
    case 0x00: t = *r; *r += 1; EAT(3); break;                  /* ,R+ */
    case 0x01: t = *r; *r += 2; EAT(4); break;                  /* ,R++ */
    case 0x02: *r -= 1; t = *r; EAT(3); break;                  /* ,-R */
    case 0x03: *r -= 2; t = *r; EAT(4); break;                  /* ,--R */
    case 0x04: t = *r; EAT(1); break;                           /* ,R */
    case 0x05: t = (uint16_t)(*r + (int8_t)c->b); EAT(2); break; /* B,R */
    case 0x06: t = (uint16_t)(*r + (int8_t)c->a); EAT(2); break; /* A,R */
    case 0x08: t = (uint16_t)(*r + (int8_t)FETCH(c)); EAT(1); break; /* n8,R */
    case 0x09: t = FETCH16(c); t = (uint16_t)(*r + t); EAT(3); break; /* n16,R */
    case 0x0b: t = (uint16_t)(*r + GETD()); EAT(5); break;      /* D,R */
    case 0x0c: { int8_t o = (int8_t)FETCH(c); t = (uint16_t)(c->pc + o); EAT(1); break; } /* n8,PCR */
    case 0x0d: { uint16_t o = FETCH16(c); t = (uint16_t)(c->pc + o); EAT(4); break; }    /* n16,PCR */
    case 0x0f: t = FETCH16(c); EAT(1); break;                   /* [n16] */
    default: t = 0; break;                                      /* 07, 0A, 0E: undefined */
    }
    if (post & 0x10) {
        uint8_t h = RD(c, t);
        t = (uint16_t)((h << 8) | RD(c, (uint16_t)(t + 1)));
        EAT(1);
    }
    return t;
}

/* ---- stack ---- */

static void push_regs(C6809 *c, uint16_t *sp, uint8_t mask, int user)
{
    uint16_t other = user ? c->s : c->u;
    if (mask & 0x80) { WR(c, --*sp, (uint8_t)c->pc); WR(c, --*sp, (uint8_t)(c->pc >> 8)); }
    if (mask & 0x40) { WR(c, --*sp, (uint8_t)other); WR(c, --*sp, (uint8_t)(other >> 8)); }
    if (mask & 0x20) { WR(c, --*sp, (uint8_t)c->y); WR(c, --*sp, (uint8_t)(c->y >> 8)); }
    if (mask & 0x10) { WR(c, --*sp, (uint8_t)c->x); WR(c, --*sp, (uint8_t)(c->x >> 8)); }
    if (mask & 0x08) WR(c, --*sp, c->dp);
    if (mask & 0x04) WR(c, --*sp, c->b);
    if (mask & 0x02) WR(c, --*sp, c->a);
    if (mask & 0x01) WR(c, --*sp, c->cc);
}

static uint16_t pull16(C6809 *c, uint16_t *sp)
{
    uint8_t h = RD(c, (*sp)++);
    return (uint16_t)((h << 8) | RD(c, (*sp)++));
}

static void pull_regs(C6809 *c, uint16_t *sp, uint8_t mask, int user)
{
    if (mask & 0x01) c->cc = RD(c, (*sp)++);
    if (mask & 0x02) c->a = RD(c, (*sp)++);
    if (mask & 0x04) c->b = RD(c, (*sp)++);
    if (mask & 0x08) c->dp = RD(c, (*sp)++);
    if (mask & 0x10) c->x = pull16(c, sp);
    if (mask & 0x20) c->y = pull16(c, sp);
    if (mask & 0x40) { uint16_t v = pull16(c, sp); if (user) c->s = v; else c->u = v; }
    if (mask & 0x80) c->pc = pull16(c, sp);
    (void)RD(c, *sp);
}

static void vector(C6809 *c, uint16_t v)
{
    uint8_t h;
    EAT(1);
    h = RD(c, v);
    c->pc = (uint16_t)((h << 8) | RD(c, (uint16_t)(v + 1)));
    EAT(1);
}

static uint16_t pending(C6809 *c)
{
    if (c->nmi_pending) return VEC_NMI;
    if (!(c->cc & CC_F) && c->firq_line) return VEC_FIRQ;
    if (!(c->cc & CC_I) && c->irq_line) return VEC_IRQ;
    return 0;
}

static void take_interrupt(C6809 *c, uint16_t v)
{
    EAT(3);
    if (v == VEC_FIRQ) {
        c->cc &= (uint8_t)~CC_E;
        push_regs(c, &c->s, 0x81, 0);
        c->cc |= CC_I | CC_F;
    } else {
        if (v == VEC_NMI) c->nmi_pending = 0;
        c->cc |= CC_E;
        push_regs(c, &c->s, 0xff, 0);
        c->cc |= (v == VEC_NMI) ? (CC_I | CC_F) : CC_I;
    }
    vector(c, v);
}

/* ---- TFR / EXG ---- */

static uint16_t tfr_read(C6809 *c, uint8_t r, int exg168)
{
    switch (r & 0x0f) {
    case 0: return GETD();
    case 1: return c->x;
    case 2: return c->y;
    case 3: return c->u;
    case 4: return c->s;
    case 5: return c->pc;
    case 8: return (uint16_t)(0xff00 | c->a);
    case 9: return (uint16_t)(0xff00 | c->b);
    case 10: return exg168 ? (uint16_t)(0xff00 | c->cc) : (uint16_t)((c->cc << 8) | c->cc);
    case 11: return exg168 ? (uint16_t)(0xff00 | c->dp) : (uint16_t)((c->dp << 8) | c->dp);
    default: return 0xffff;
    }
}

static void tfr_write(C6809 *c, uint8_t r, uint16_t v)
{
    switch (r & 0x0f) {
    case 0: SETD(v); break;
    case 1: c->x = v; break;
    case 2: c->y = v; break;
    case 3: c->u = v; break;
    case 4: c->s = v; break;
    case 5: c->pc = v; break;
    case 8: c->a = (uint8_t)v; break;
    case 9: c->b = (uint8_t)v; break;
    case 10: c->cc = (uint8_t)v; break;
    case 11: c->dp = (uint8_t)v; break;
    default: break;
    }
}

/* ---- read-modify-write group (00-0F, 40-7F) ---- */

/* mode: 0 = memory at ea, 1 = A, 2 = B */
static void rmw(C6809 *c, uint8_t op, int mode, uint16_t ea)
{
    uint8_t t, r;
    uint8_t *reg = mode == 1 ? &c->a : mode == 2 ? &c->b : 0;
    uint8_t fn = op & 0x0f;

    if (fn == 0x0e) {
        if (!reg) { c->pc = ea; return; }  /* JMP */
        /* XCLR (4E/5E): like CLR, carry kept */
        EAT(1);
        c->cc &= (uint8_t)~CC_NZV;
        c->cc |= CC_Z;
        *reg = 0;
        return;
    }
    t = reg ? *reg : RD(c, ea);
    switch (fn) {
    case 0x02: /* XNC: COM if carry, else NEG */
        if (c->cc & CC_C) goto com;
        /* fall through */
    case 0x00: case 0x01:
        r = f8(c, CC_NZVC, 0, t, (uint32_t)(-(int32_t)t));
        break;
    case 0x03:
    com:
        c->cc &= (uint8_t)~CC_V;
        c->cc |= CC_C;
        r = f8(c, CC_NZ, 0, (uint8_t)~t, (uint8_t)~t);
        break;
    case 0x04: case 0x05:
        c->cc = (uint8_t)((c->cc & ~CC_C) | (t & 1));
        r = f8(c, CC_NZ, 0, (uint8_t)(t >> 1), (uint8_t)(t >> 1));
        break;
    case 0x06: {
        uint8_t v = (uint8_t)((t >> 1) | ((c->cc & CC_C) ? 0x80 : 0));
        c->cc = (uint8_t)((c->cc & ~CC_C) | (t & 1));
        r = f8(c, CC_NZ, 0, v, v);
        break;
    }
    case 0x07: {
        uint8_t v = (uint8_t)((t >> 1) | (t & 0x80));
        c->cc = (uint8_t)((c->cc & ~CC_C) | (t & 1));
        r = f8(c, CC_NZ, 0, v, v);
        break;
    }
    case 0x08:
        r = f8(c, CC_NZVC, t, t, (uint32_t)t << 1);
        break;
    case 0x09: {
        uint32_t v = ((uint32_t)t << 1) | ((c->cc & CC_C) ? 1u : 0u);
        c->cc = (uint8_t)((c->cc & ~CC_C) | ((t & 0x80) ? CC_C : 0));
        r = f8(c, CC_NZV, t, t, v);
        break;
    }
    case 0x0b: /* XDEC: carry = operand != 0 */
        if (t) c->cc |= CC_C; else c->cc &= (uint8_t)~CC_C;
        /* fall through */
    case 0x0a:
        r = f8(c, CC_NZV, t, 1, (uint32_t)((int32_t)t - 1));
        break;
    case 0x0c:
        r = f8(c, CC_NZV, t, 1, (uint32_t)t + 1);
        break;
    case 0x0d: /* TST */
        NZV8(t);
        EAT(reg ? 1 : 2);
        return;
    default: /* 0x0f CLR */
        c->cc &= (uint8_t)~CC_NZVC;
        c->cc |= CC_Z;
        r = 0;
        break;
    }
    EAT(1);
    if (reg) *reg = r; else WR(c, ea, r);
}

/* ---- 80-FF: register/memory group ---- */

/* operand address for modes 1-3 (direct, indexed, extended); mode 0 = immediate */
static uint16_t ea_mode(C6809 *c, int mode)
{
    switch (mode) {
    case 1: return ea_direct(c);
    case 2: return ea_indexed(c);
    default: return ea_extended(c);
    }
}

static uint8_t op8(C6809 *c, int mode, uint16_t *ea)
{
    if (mode == 0) return FETCH(c);
    *ea = ea_mode(c, mode);
    return RD(c, *ea);
}

static uint16_t op16(C6809 *c, int mode)
{
    uint16_t ea;
    uint8_t h;
    if (mode == 0) return FETCH16(c);
    ea = ea_mode(c, mode);
    h = RD(c, ea);
    return (uint16_t)((h << 8) | RD(c, (uint16_t)(ea + 1)));
}

static void alu8(C6809 *c, uint8_t fn, uint8_t *reg, int mode)
{
    uint16_t ea = 0;
    uint8_t m;

    if (fn == 0x07) {
        if (mode == 0) { (void)FETCH(c); NZV8(*reg); return; } /* XST8 */
        ea = ea_mode(c, mode);
        WR(c, ea, NZV8(*reg));
        return;
    }
    m = op8(c, mode, &ea);
    switch (fn) {
    case 0x0: *reg = f8(c, CC_NZVC, *reg, m, (uint32_t)((int32_t)*reg - m)); break;
    case 0x1: f8(c, CC_NZVC, *reg, m, (uint32_t)((int32_t)*reg - m)); break;
    case 0x2: *reg = f8(c, CC_NZVC, *reg, m, (uint32_t)((int32_t)*reg - m - ((c->cc & CC_C) ? 1 : 0))); break;
    case 0x4: c->cc &= (uint8_t)~CC_V; *reg = f8(c, CC_NZ, 0, *reg, (uint8_t)(*reg & m)); break;
    case 0x5: c->cc &= (uint8_t)~CC_V; f8(c, CC_NZ, 0, *reg, (uint8_t)(*reg & m)); break;
    case 0x6: *reg = m; NZV8(m); break;
    case 0x8: c->cc &= (uint8_t)~CC_V; *reg = f8(c, CC_NZ, 0, *reg, (uint8_t)(*reg ^ m)); break;
    case 0x9: *reg = f8(c, CC_HNZVC, *reg, m, (uint32_t)*reg + m + ((c->cc & CC_C) ? 1 : 0)); break;
    case 0xa: c->cc &= (uint8_t)~CC_V; *reg = f8(c, CC_NZ, 0, *reg, (uint8_t)(*reg | m)); break;
    default: *reg = f8(c, CC_HNZVC, *reg, m, (uint32_t)*reg + m); break; /* 0xb ADD */
    }
}

/* 16-bit: kind 0 = SUB, 1 = ADD, 2 = CMP, 3 = XADD (flags only) */
/* 16 bit register by number: 0 D, 1 X, 2 Y, 3 U, 4 S. ADD16/SUB16/CMP16/ST16 read the register
   after the operand (MAME regop16()), so e.g. CMPX ,X++ and STX ,--X use the changed X */
static uint16_t reg16(const C6809 *c, int r)
{
    switch (r) {
    case 0: return GETD();
    case 1: return c->x;
    case 2: return c->y;
    case 3: return c->u;
    default: return c->s;
    }
}

static uint16_t arith16(C6809 *c, int kind, int reg, int mode)
{
    uint16_t m = op16(c, mode);
    uint16_t r = reg16(c, reg);
    uint16_t v;
    if (kind == 0 || kind == 2)
        v = f16(c, CC_NZVC, r, m, (uint32_t)((int32_t)r - m));
    else
        v = f16(c, CC_NZVC, r, m, (uint32_t)r + m);
    EAT(1);
    return (kind == 0 || kind == 1) ? v : r;
}

static uint16_t ld16(C6809 *c, int mode)
{
    uint16_t v = op16(c, mode);
    NZV16(v);
    return v;
}

static void st16(C6809 *c, int reg, int mode)
{
    uint16_t v;
    if (mode == 0) { /* XST16 */
        (void)FETCH(c);
        v = reg16(c, reg);
        WR(c, c->pc++, (uint8_t)v);
        NZV16(v);
        return;
    }
    {
        uint16_t ea = ea_mode(c, mode);
        v = reg16(c, reg);
        WR(c, ea, (uint8_t)(v >> 8));
        WR(c, (uint16_t)(ea + 1), (uint8_t)v);
        NZV16(v);
    }
}

static void gosub(C6809 *c, uint16_t ea)
{
    WR(c, --c->s, (uint8_t)c->pc);
    WR(c, --c->s, (uint8_t)(c->pc >> 8));
    c->pc = ea;
}

static int cond(C6809 *c, uint8_t op)
{
    uint8_t cc = c->cc;
    int r;
    switch ((op >> 1) & 7) {
    case 0: r = 1; break;                                       /* BRA */
    case 1: r = !(cc & (CC_Z | CC_C)); break;                   /* BHI */
    case 2: r = !(cc & CC_C); break;                            /* BCC */
    case 3: r = !(cc & CC_Z); break;                            /* BNE */
    case 4: r = !(cc & CC_V); break;                            /* BVC */
    case 5: r = !(cc & CC_N); break;                            /* BPL */
    case 6: r = !(cc & CC_N) == !(cc & CC_V); break;            /* BGE */
    default: r = (!(cc & CC_N) == !(cc & CC_V)) && !(cc & CC_Z); break; /* BGT */
    }
    return (op & 1) ? !r : r;
}

static void lbranch(C6809 *c, int taken)
{
    uint16_t off = FETCH16(c);
    EAT(1);
    if (taken) {
        c->pc = (uint16_t)(c->pc + off);
        EAT(1);
    }
}

static void swi(C6809 *c, uint16_t v, int mask_if)
{
    c->cc |= CC_E;
    EAT(2);
    push_regs(c, &c->s, 0xff, 0);
    if (mask_if) c->cc |= CC_I | CC_F;
    vector(c, v);
}

static void exec_page10(C6809 *c);
static void exec_page11(C6809 *c);

static void exec(C6809 *c, uint8_t op)
{
    uint8_t hi = op >> 4, lo = op & 0x0f;

    if (op >= 0x80) {
        int mode = hi & 3;
        int bside = hi >= 0x0c;
        uint8_t *reg = bside ? &c->b : &c->a;
        switch (lo) {
        case 0x3:
            if (bside) SETD(arith16(c, 1, 0, mode));
            else SETD(arith16(c, 0, 0, mode));
            return;
        case 0xc:
            if (bside) SETD(ld16(c, mode));
            else arith16(c, 2, 1, mode);
            return;
        case 0xd:
            if (bside) {
                if (mode == 0) { c->freerun = 1; return; }
                st16(c, 0, mode);
            } else if (mode == 0) { /* BSR */
                int8_t o = (int8_t)FETCH(c);
                EAT(3);
                gosub(c, (uint16_t)(c->pc + o));
            } else { /* JSR */
                uint16_t ea = ea_mode(c, mode);
                EAT(2);
                gosub(c, ea);
            }
            return;
        case 0xe: {
            uint16_t v = ld16(c, mode);
            if (bside) c->u = v; else c->x = v;
            return;
        }
        case 0xf:
            st16(c, bside ? 3 : 1, mode);
            return;
        default:
            alu8(c, lo, reg, mode);
            return;
        }
    }

    switch (hi) {
    case 0x0: rmw(c, op, 0, ea_direct(c)); return;
    case 0x4: rmw(c, op, 1, 0); return;
    case 0x5: rmw(c, op, 2, 0); return;
    case 0x6: rmw(c, op, 0, ea_indexed(c)); return;
    case 0x7: rmw(c, op, 0, ea_extended(c)); return;
    case 0x2: {
        int8_t o = (int8_t)FETCH(c);
        EAT(1);
        if (cond(c, op)) c->pc = (uint16_t)(c->pc + o);
        return;
    }
    default: break;
    }

    switch (op) {
    case 0x10: exec_page10(c); return;
    case 0x11: exec_page11(c); return;
    case 0x12: case 0x1b: EAT(1); return;                   /* NOP */
    case 0x13:                                              /* SYNC */
        EAT(1);
        c->waiting = 2;
        return;
    case 0x14: case 0x15: c->freerun = 1; return;
    case 0x16: lbranch(c, 1); return;                       /* LBRA */
    case 0x17: {                                            /* LBSR */
        uint16_t o = FETCH16(c);
        EAT(4);
        gosub(c, (uint16_t)(c->pc + o));
        return;
    }
    case 0x18: {                                            /* X18 */
        uint8_t t = FETCH(c);
        t = (uint8_t)((c->cc & t) << 1);
        c->cc = (uint8_t)(t | ((c->cc & 0x04) >> 1));
        return;
    }
    case 0x19: {                                            /* DAA */
        uint16_t t, cf = 0;
        uint8_t msn = c->a & 0xf0, lsn = c->a & 0x0f;
        if (lsn > 0x09 || (c->cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 0x09) cf |= 0x60;
        if (msn > 0x90 || (c->cc & CC_C)) cf |= 0x60;
        t = (uint16_t)(c->a + cf);
        c->cc &= (uint8_t)~CC_V;
        if (t & 0x100) c->cc |= CC_C;
        c->a = f8(c, CC_NZ, 0, (uint8_t)t, (uint8_t)t);
        EAT(1);
        return;
    }
    case 0x1a: c->cc |= FETCH(c); EAT(1); return;          /* ORCC */
    case 0x1c: c->cc &= FETCH(c); EAT(1); return;          /* ANDCC */
    case 0x1d: {                                            /* SEX */
        uint16_t v = (uint16_t)(int16_t)(int8_t)c->b;
        SETD(f16(c, CC_NZ, 0, v, v));
        EAT(1);
        return;
    }
    case 0x1e: {                                            /* EXG */
        uint8_t p = FETCH(c);
        int e168 = !(p & 0x80);
        uint16_t r1 = tfr_read(c, (uint8_t)(p >> 4), e168);
        uint16_t r2 = tfr_read(c, p, e168);
        tfr_write(c, p, r1);
        tfr_write(c, (uint8_t)(p >> 4), r2);
        EAT(6);
        return;
    }
    case 0x1f: {                                            /* TFR */
        uint8_t p = FETCH(c);
        tfr_write(c, p, tfr_read(c, (uint8_t)(p >> 4), 0));
        if ((p & 0x0f) == 4) c->lds_seen = 1;
        EAT(4);
        return;
    }
    case 0x30: case 0x31: {                                 /* LEAX, LEAY */
        uint16_t ea = ea_indexed(c);
        f16(c, CC_Z, 0, ea, ea);
        if (op == 0x30) c->x = ea; else c->y = ea;
        EAT(1);
        return;
    }
    case 0x32: case 0x33: {                                 /* LEAS, LEAU */
        uint16_t ea = ea_indexed(c);
        if (op == 0x32) { c->s = ea; c->lds_seen = 1; } else c->u = ea;
        EAT(1);
        return;
    }
    case 0x34: {                                            /* PSHS */
        uint8_t m = FETCH(c);
        EAT(2);
        (void)RD(c, c->s);
        push_regs(c, &c->s, m, 0);
        return;
    }
    case 0x35: {                                            /* PULS */
        uint8_t m = FETCH(c);
        EAT(2);
        pull_regs(c, &c->s, m, 0);
        return;
    }
    case 0x36: {                                            /* PSHU */
        uint8_t m = FETCH(c);
        EAT(2);
        (void)RD(c, c->u);
        push_regs(c, &c->u, m, 1);
        return;
    }
    case 0x37: {                                            /* PULU */
        uint8_t m = FETCH(c);
        EAT(2);
        pull_regs(c, &c->u, m, 1);
        return;
    }
    case 0x38: EAT(1); c->cc &= FETCH(c); EAT(1); return;  /* XANDCC */
    case 0x39: EAT(1); pull_regs(c, &c->s, 0x80, 0); return; /* RTS */
    case 0x3a: c->x = (uint16_t)(c->x + c->b); EAT(2); return; /* ABX */
    case 0x3b: {                                            /* RTI */
        uint8_t m;
        EAT(1);
        c->cc = RD(c, c->s++);
        m = (uint8_t)(((c->cc & CC_E) ? 0xff : 0x81) & ~0x01);
        pull_regs(c, &c->s, m, 0);
        return;
    }
    case 0x3c:                                              /* CWAI */
        c->cc &= FETCH(c);
        EAT(2);
        c->cc |= CC_E;
        push_regs(c, &c->s, 0xff, 0);
        c->waiting = 1;
        return;
    case 0x3d: {                                            /* MUL */
        uint16_t r = (uint16_t)(c->a * c->b);
        SETD(f16(c, CC_Z, 0, r, r));
        if (r & 0x80) c->cc |= CC_C; else c->cc &= (uint8_t)~CC_C;
        EAT(10);
        return;
    }
    case 0x3e:                                              /* XRES */
        EAT(2);
        push_regs(c, &c->s, 0xff, 0);
        vector(c, VEC_RESET);
        return;
    case 0x3f: swi(c, VEC_SWI, 1); return;
    default: return;                                        /* illegal: only the fetch */
    }
}

static void exec_page10(C6809 *c)
{
    uint8_t op = FETCH(c);
    int mode = (op >> 4) & 3;

    if (op == 0x10 || op == 0x11) { exec_page10(c); return; }
    if ((op & 0xf0) == 0x20) { lbranch(c, cond(c, op)); return; }
    switch (op) {
    case 0x3e:                                              /* XSWI2 */
        EAT(2);
        push_regs(c, &c->s, 0xff, 0);
        vector(c, VEC_SWI2);
        return;
    case 0x3f: swi(c, VEC_SWI2, 0); return;
    case 0x83: case 0x93: case 0xa3: case 0xb3: arith16(c, 2, 0, mode); return; /* CMPD */
    case 0x8c: case 0x9c: case 0xac: case 0xbc: arith16(c, 2, 2, mode); return;  /* CMPY */
    case 0x8e: case 0x9e: case 0xae: case 0xbe: c->y = ld16(c, mode); return;       /* LDY */
    case 0x8f: case 0x9f: case 0xaf: case 0xbf: st16(c, 2, mode); return;        /* STY */
    case 0xce: case 0xde: case 0xee: case 0xfe: c->s = ld16(c, mode); c->lds_seen = 1; return; /* LDS */
    case 0xcf: case 0xdf: case 0xef: case 0xff: st16(c, 4, mode); return;        /* STS */
    case 0x87: { uint16_t ea; (void)op8(c, 0, &ea); NZV8(c->a); return; }          /* XST8 */
    case 0xc7: { uint16_t ea; (void)op8(c, 0, &ea); NZV8(c->b); return; }
    case 0xc3: case 0xd3: case 0xe3: case 0xf3: arith16(c, 3, 0, mode); return; /* XADD16 */
    default: exec(c, FETCH(c)); return;                     /* MAME: DISPATCH01 fetches a new opcode */
    }
}

static void exec_page11(C6809 *c)
{
    uint8_t op = FETCH(c);
    int mode = (op >> 4) & 3;

    if (op == 0x10 || op == 0x11) { exec_page11(c); return; }
    switch (op) {
    case 0x3e:                                              /* XFIRQ */
        EAT(2);
        push_regs(c, &c->s, 0xff, 0);
        vector(c, VEC_FIRQ);
        return;
    case 0x3f: swi(c, VEC_SWI3, 0); return;
    case 0x83: case 0x93: case 0xa3: case 0xb3: arith16(c, 2, 3, mode); return;  /* CMPU */
    case 0x8c: case 0x9c: case 0xac: case 0xbc: arith16(c, 2, 4, mode); return;  /* CMPS */
    case 0x87: { uint16_t ea; (void)op8(c, 0, &ea); NZV8(c->a); return; }
    case 0xc7: { uint16_t ea; (void)op8(c, 0, &ea); NZV8(c->b); return; }
    case 0x8f: st16(c, 1, 0); return;
    case 0xcf: st16(c, 3, 0); return;
    case 0xc3: case 0xd3: case 0xe3: case 0xf3: arith16(c, 3, 3, mode); return;
    default: exec(c, FETCH(c)); return;
    }
}

/* ---- public ---- */

void m6809_init(C6809 *c)
{
    memset(c, 0, sizeof(*c));
}

void m6809_reset(C6809 *c)
{
    c->nmi_pending = 0;
    c->lds_seen = 0;
    c->freerun = 0;
    c->waiting = 0;
    c->dp = 0;
    c->cc |= CC_I | CC_F;
    {
        int32_t ic = c->icount;
        vector(c, VEC_RESET);   /* MAME: reset state then the vector fetch (4 cycles) */
        c->total += (uint32_t)(ic - c->icount);
    }
}

void m6809_set_nmi(C6809 *c, int state)
{
    if (state && !c->nmi_line && c->lds_seen) c->nmi_pending = 1;
    c->nmi_line = (uint8_t)(state != 0);
}

int32_t m6809_step(C6809 *c)
{
    int32_t start = c->icount;
    uint16_t v;

    if (c->freerun) {
        c->icount = start > 0 ? 0 : start - 1;
        goto done;
    }
    if (c->waiting == 1) {                                  /* CWAI: state already pushed */
        v = pending(c);
        if (!v) {
            c->icount = start > 0 ? 0 : start - 1;
            goto done;
        }
        c->waiting = 0;
        if (v == VEC_NMI) c->nmi_pending = 0;
        c->cc |= (uint8_t)(CC_I | (v != VEC_IRQ ? CC_F : 0));
        vector(c, v);
        goto done;
    }
    if (c->waiting == 2) {                                  /* SYNC */
        if (!c->nmi_pending && !c->firq_line && !c->irq_line) {
            c->icount = start > 0 ? 0 : start - 1;
            goto done;
        }
        c->waiting = 0;
        EAT(1);
        goto done;
    }
    v = pending(c);
    if (v) {
        take_interrupt(c, v);
        goto done;
    }
    c->ppc = c->pc;
    if (c->hook) c->hook(c);
    exec(c, FETCH(c));
done:
    c->total += (uint32_t)(start - c->icount);
    return start - c->icount;
}

static void idle_regs(const C6809 *c, uint8_t r[12])
{
    r[0] = c->a; r[1] = c->b; r[2] = c->dp; r[3] = c->cc;
    r[4] = (uint8_t)(c->x >> 8); r[5] = (uint8_t)c->x; r[6] = (uint8_t)(c->y >> 8); r[7] = (uint8_t)c->y;
    r[8] = (uint8_t)(c->u >> 8); r[9] = (uint8_t)c->u; r[10] = (uint8_t)(c->s >> 8); r[11] = (uint8_t)c->s;
}

/* at a watched loop address: skip the whole turns that are left (see m6809.h) */
static void idle_check(C6809 *c)
{
    uint8_t r[12];
    int i;
    for (i = 0; i < c->nidle; i++)
        if (c->idle_pc[i] == c->pc)
            break;
    if (i == c->nidle)
        return;
    {
        /* the code must be the expected loop (ROM banks) */
        int k;
        for (k = 0; k < c->idle_len[i]; k++) {
            uint16_t a = (uint16_t)(c->pc + k);
            const uint8_t *p = c->rpage[a >> 8];
            if (!p || p[a & 0xff] != c->idle_sig[i][k]) {
                c->idle_valid = 0;
                return;
            }
        }
    }
    if (c->waiting || c->freerun || pending(c)) {
        c->idle_valid = 0;                              /* an interrupt is taken first */
        return;
    }
    idle_regs(c, r);
    if (c->idle_valid && c->idle_at == c->pc && !memcmp(r, c->idle_regs, 12)) {
        int32_t turn = c->idle_icount - c->icount;     /* cycles of one turn */
        if (turn > 0 && c->icount > turn) {
            int32_t k = (c->icount - 1) / turn;
            c->icount -= k * turn;
            c->total += (uint32_t)(k * turn);
            c->idle_skipped += (uint32_t)(k * turn);
        }
    }
    c->idle_valid = 1;
    c->idle_at = c->pc;
    c->idle_icount = c->icount;
    memcpy(c->idle_regs, r, 12);
}

void m6809_add_idle_loop(C6809 *c, uint16_t addr, const uint8_t *sig, int len)
{
    if (c->nidle >= 8) return;
    c->idle_pc[c->nidle] = addr;
    c->idle_sig[c->nidle] = sig;
    c->idle_len[c->nidle] = (uint8_t)len;
    c->idle_map[addr >> 3] |= (uint8_t)(1 << (addr & 7));
    c->nidle++;
}

void m6809_map_page(C6809 *c, int p, const uint8_t *r, uint8_t *w)
{
    c->rpage[p] = r;
    c->wpage[p] = w;
    /* biased pointers for the assembler core (only ever used with addresses of page p) */
    c->rbase[p] = r ? r - p * 256 : 0;
    c->wbase[p] = w ? w - p * 256 : 0;
}

#define OFS(f) ((int)((const char *)&((C6809 *)0)->f - (const char *)0))

int m6809_layout_ok(void)
{
    return OFS(rpage) == 0 && OFS(wpage) == 1024 && OFS(rbase) == 2048 && OFS(wbase) == 3072 &&
           OFS(read) == 4096 && OFS(write) == 4100 && OFS(ctx) == 4104 && OFS(hook) == 4108 &&
           OFS(icount) == 4112 && OFS(total) == 4116 && OFS(idle_sig) == 4120 &&
           OFS(idle_icount) == 4152 && OFS(idle_skipped) == 4156 && OFS(x) == 4160 &&
           OFS(y) == 4162 && OFS(u) == 4164 && OFS(s) == 4166 && OFS(pc) == 4168 && OFS(ppc) == 4170 &&
           OFS(idle_pc) == 4172 && OFS(idle_at) == 4188 && OFS(a) == 4190 && OFS(b) == 4191 &&
           OFS(dp) == 4192 && OFS(cc) == 4193 && OFS(nmi_line) == 4194 && OFS(firq_line) == 4195 &&
           OFS(irq_line) == 4196 && OFS(nmi_pending) == 4197 && OFS(lds_seen) == 4198 &&
           OFS(waiting) == 4199 && OFS(freerun) == 4200 && OFS(idle_len) == 4201 &&
           OFS(nidle) == 4209 && OFS(idle_valid) == 4210 && OFS(idle_regs) == 4211 &&
           OFS(idle_map) == M6809_OFS_IDLE_MAP;
}

/* memory accesses of the assembler core that have no direct page (I/O) */
uint32_t m6809_rd_slow(C6809 *c, uint32_t addr)
{
    return c->read(c->ctx, (uint16_t)addr);
}

void m6809_wr_slow(C6809 *c, uint32_t addr, uint32_t data)
{
    c->write(c->ctx, (uint16_t)addr, (uint8_t)data);
}

/* the assembler core reached an address marked in idle_map (the registers are in the struct) */
void m6809_idle_hit(C6809 *c)
{
    idle_check(c);
}

int32_t m6809_run(C6809 *c, int32_t cycles)
{
    int32_t start;
    c->icount += cycles;
    start = c->icount;
    c->idle_valid = 0;                                  /* the world may have changed */
    if (c->nidle) {
        while (c->icount > 0) {
            idle_check(c);
            m6809_step(c);
        }
    } else {
        while (c->icount > 0)
            m6809_step(c);
    }
    return start - c->icount;
}
