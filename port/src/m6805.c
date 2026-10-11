/* Motorola 6805 (HMOS) emulator, see m6805.h. Semantics and cycles: MAME 0.289
 * (doc/mame0289/6805ops.hxx, m6805.cpp s_hmos_cycles, m68705.cpp interrupt()).
 */
#include "m6805.h"
#include <string.h>

#define CF M6805_CC_C
#define ZF M6805_CC_Z
#define NF M6805_CC_N
#define IF M6805_CC_I
#define HF M6805_CC_H

#define AMASK 0x7ff
#define SP_MASK 0x7f
#define SP_LOW 0x60
#define VEC_TIMER 0x7f8
#define VEC_INT 0x7fa
#define VEC_SWI 0x7fc
#define VEC_RESET 0x7fe

static const uint8_t cycles[256] = {
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
     7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
     4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
     6, 2, 2, 6, 6, 2, 6, 6, 6, 6, 6, 2, 6, 6, 2, 6,
     4, 2, 2, 4, 4, 2, 4, 4, 4, 4, 4, 2, 4, 4, 2, 4,
     4, 2, 2, 4, 4, 2, 4, 4, 4, 4, 4, 2, 4, 4, 2, 4,
     7, 2, 2, 7, 7, 2, 7, 7, 7, 7, 7, 2, 7, 7, 2, 7,
     6, 2, 2, 6, 6, 2, 6, 6, 6, 6, 6, 2, 6, 6, 2, 6,
     9, 6, 2,11, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
     2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 8, 2, 2,
     4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 3, 7, 4, 5,
     5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 4, 8, 5, 6,
     6, 6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 6, 5, 9, 6, 7,
     5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 4, 8, 5, 6,
     4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4, 4, 3, 7, 4, 5
};  /* illegal opcodes (XX in MAME) are counted as 2 */

static uint8_t rm(M6805 *c, uint16_t a)
{
    return c->read(c->ctx, (uint16_t)(a & AMASK));
}

static void wm(M6805 *c, uint16_t a, uint8_t v)
{
    c->write(c->ctx, (uint16_t)(a & AMASK), v);
}

static uint8_t fetch(M6805 *c)
{
    uint16_t a = (uint16_t)(c->pc++ & AMASK);
    return a >= 0x80 ? c->rom[a] : c->read(c->ctx, a);
}

static void push(M6805 *c, uint8_t v)
{
    wm(c, c->s, v);
    if (--c->s < SP_LOW) c->s = SP_MASK;
}

static uint8_t pull(M6805 *c)
{
    if (++c->s > SP_MASK) c->s = SP_LOW;
    return rm(c, c->s);
}

static void push_pc(M6805 *c)
{
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)(c->pc >> 8));
}

static uint16_t rm16(M6805 *c, uint16_t a)
{
    uint8_t h = rm(c, a);
    return (uint16_t)((h << 8) | rm(c, (uint16_t)(a + 1)));
}

#define NZ8(v) do { uint8_t v_ = (uint8_t)(v); c->cc |= (uint8_t)((v_ & 0x80) >> 5); if (!v_) c->cc |= ZF; } while (0)
#define CLR_NZ() (c->cc &= (uint8_t)~(NF | ZF))
#define CLR_NZC() (c->cc &= (uint8_t)~(NF | ZF | CF))

/* effective address: mode 0 imm (returns PC++), 1 direct, 2 extended, 3 ix2, 4 ix1, 5 ix */
static uint16_t ea(M6805 *c, int mode)
{
    uint16_t e;
    switch (mode) {
    case 0: return c->pc++;
    case 1: return fetch(c);
    case 2: e = (uint16_t)(fetch(c) << 8); return (uint16_t)(e | fetch(c));
    case 3: e = (uint16_t)(fetch(c) << 8); e |= fetch(c); return (uint16_t)(e + c->x);
    case 4: e = fetch(c); return (uint16_t)(e + c->x);
    default: return c->x;
    }
}

static uint8_t arg(M6805 *c, int mode, uint16_t *e)
{
    if (mode == 0) return fetch(c);
    *e = ea(c, mode);
    return rm(c, *e);
}

/* read-modify-write group: fn = low nibble, val in/out */
static int rmw(M6805 *c, uint8_t fn, uint8_t *v)
{
    uint8_t t = *v;
    uint16_t r;
    switch (fn) {
    case 0x0: r = (uint16_t)(-(int)t); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; *v = (uint8_t)r; return 1;
    case 0x3: t = (uint8_t)~t; CLR_NZ(); NZ8(t); c->cc |= CF; *v = t; return 1;
    case 0x4: CLR_NZC(); c->cc |= t & 1; t >>= 1; if (!t) c->cc |= ZF; *v = t; return 1;
    case 0x6: { uint8_t q = (uint8_t)((c->cc & CF) << 7); CLR_NZC(); c->cc |= t & 1; q |= t >> 1; NZ8(q); *v = q; return 1; }
    case 0x7: CLR_NZC(); c->cc |= t & 1; t = (uint8_t)((t >> 1) | (t & 0x80)); NZ8(t); *v = t; return 1;
    case 0x8: r = (uint16_t)(t << 1); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; *v = (uint8_t)r; return 1;
    case 0x9: r = (uint16_t)((c->cc & CF) | (t << 1)); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; *v = (uint8_t)r; return 1;
    case 0xa: t--; CLR_NZ(); NZ8(t); *v = t; return 1;
    case 0xc: t++; CLR_NZ(); NZ8(t); *v = t; return 1;
    case 0xd: CLR_NZ(); NZ8(t); return 0;                 /* TST: no write */
    case 0xf: CLR_NZ(); c->cc |= ZF; *v = 0; return 1;
    default: return -1;                                   /* illegal */
    }
}

static void interrupt(M6805 *c)
{
    push_pc(c);
    push(c, c->x);
    push(c, c->a);
    push(c, c->cc);
    c->cc |= IF;
    if (c->pending & M6805_INT_IRQ) {
        c->pending &= (uint8_t)~M6805_INT_IRQ;
        c->pc = rm16(c, VEC_INT);
    } else {
        c->pc = rm16(c, VEC_TIMER);
    }
}

static int branch_cond(M6805 *c, uint8_t op)
{
    int r;
    switch ((op >> 1) & 7) {
    case 0: r = 1; break;
    case 1: r = !(c->cc & (CF | ZF)); break;
    case 2: r = !(c->cc & CF); break;
    case 3: r = !(c->cc & ZF); break;
    case 4: r = !(c->cc & HF); break;
    case 5: r = !(c->cc & NF); break;
    case 6: r = !(c->cc & IF); break;
    default: r = c->irq_pin != 0; break;                  /* BIL: /INT low (asserted) */
    }
    return (op & 1) ? !r : r;
}

static void exec(M6805 *c, uint8_t op)
{
    uint8_t hi = op >> 4, lo = op & 0x0f;
    uint16_t e = 0;

    switch (hi) {
    case 0x0: {                                           /* BRSET/BRCLR */
        uint8_t r, t, bit = (uint8_t)(1 << (lo >> 1));
        e = fetch(c);
        r = rm(c, e);
        t = fetch(c);
        if (!(lo & 1)) {
            c->cc &= (uint8_t)~CF;
            if (r & bit) { c->cc |= CF; c->pc = (uint16_t)(c->pc + (int8_t)t); }
        } else {
            c->cc |= CF;
            if (!(r & bit)) { c->cc &= (uint8_t)~CF; c->pc = (uint16_t)(c->pc + (int8_t)t); }
        }
        return;
    }
    case 0x1: {                                           /* BSET/BCLR */
        uint8_t t, bit = (uint8_t)(1 << (lo >> 1));
        e = fetch(c);
        t = rm(c, e);
        wm(c, e, (lo & 1) ? (uint8_t)(t & ~bit) : (uint8_t)(t | bit));
        return;
    }
    case 0x2: {
        uint8_t t = fetch(c);
        if (branch_cond(c, op)) c->pc = (uint16_t)(c->pc + (int8_t)t);
        return;
    }
    case 0x3: case 0x6: case 0x7: {
        int mode = hi == 3 ? 1 : hi == 6 ? 4 : 5;
        uint8_t v;
        if (lo == 0x1 || lo == 0x2 || lo == 0x5 || lo == 0xb || lo == 0xe) return;
        e = ea(c, mode);
        if (lo == 0xf) {                                  /* CLR: no read */
            v = 0;
            rmw(c, lo, &v);
            wm(c, e, 0);
            return;
        }
        v = rm(c, e);
        if (rmw(c, lo, &v) > 0) wm(c, e, v);
        return;
    }
    case 0x4: rmw(c, lo, &c->a); return;
    case 0x5: rmw(c, lo, &c->x); return;
    case 0x8:
        switch (op) {
        case 0x80: c->cc = pull(c); c->a = pull(c); c->x = pull(c);
            { uint8_t h = pull(c); c->pc = (uint16_t)((h << 8) | pull(c)); } return;
        case 0x81: { uint8_t h = pull(c); c->pc = (uint16_t)((h << 8) | pull(c)); } return;
        case 0x83: push_pc(c); push(c, c->x); push(c, c->a); push(c, c->cc); c->cc |= IF;
            c->pc = rm16(c, VEC_SWI); return;
        default: return;
        }
    case 0x9:
        switch (op) {
        case 0x97: c->x = c->a; return;
        case 0x98: c->cc &= (uint8_t)~CF; return;
        case 0x99: c->cc |= CF; return;
        case 0x9a: c->cc &= (uint8_t)~IF; return;
        case 0x9b: c->cc |= IF; return;
        case 0x9c: c->s = SP_MASK; return;
        case 0x9f: c->a = c->x; return;
        default: return;                                  /* 9D NOP, illegal */
        }
    default: break;
    }

    /* A0-FF: register/memory group */
    {
        int mode = hi == 0xa ? 0 : hi == 0xb ? 1 : hi == 0xc ? 2 : hi == 0xd ? 3 : hi == 0xe ? 4 : 5;
        uint8_t t;
        uint16_t r;
        if (mode == 0 && (lo == 0x7 || lo == 0xc || lo == 0xf)) return;   /* illegal */
        switch (lo) {
        case 0x0: t = arg(c, mode, &e); r = (uint16_t)(c->a - t); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; c->a = (uint8_t)r; return;
        case 0x1: t = arg(c, mode, &e); r = (uint16_t)(c->a - t); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; return;
        case 0x2: t = arg(c, mode, &e); r = (uint16_t)(c->a - t - (c->cc & CF)); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; c->a = (uint8_t)r; return;
        case 0x3: t = arg(c, mode, &e); r = (uint16_t)(c->x - t); CLR_NZC(); NZ8(r); if (r & 0x100) c->cc |= CF; return;
        case 0x4: t = arg(c, mode, &e); c->a &= t; CLR_NZ(); NZ8(c->a); return;
        case 0x5: t = arg(c, mode, &e); CLR_NZ(); NZ8(c->a & t); return;
        case 0x6: c->a = arg(c, mode, &e); CLR_NZ(); NZ8(c->a); return;
        case 0x7: CLR_NZ(); NZ8(c->a); e = ea(c, mode); wm(c, e, c->a); return;
        case 0x8: t = arg(c, mode, &e); c->a ^= t; CLR_NZ(); NZ8(c->a); return;
        case 0x9: case 0xb: {
            uint8_t a = c->a;
            t = arg(c, mode, &e);
            r = (uint16_t)(a + t + (lo == 0x9 ? (c->cc & CF) : 0));
            c->cc &= (uint8_t)~(HF | NF | ZF | CF);
            c->cc |= (uint8_t)((a ^ t ^ r) & 0x10);
            NZ8(r);
            if (r & 0x100) c->cc |= CF;
            c->a = (uint8_t)r;
            return;
        }
        case 0xa: t = arg(c, mode, &e); c->a |= t; CLR_NZ(); NZ8(c->a); return;
        case 0xc: c->pc = ea(c, mode); return;            /* JMP */
        case 0xd:
            if (mode == 0) {                              /* BSR */
                t = fetch(c);
                push_pc(c);
                c->pc = (uint16_t)(c->pc + (int8_t)t);
            } else {                                      /* JSR */
                e = ea(c, mode);
                push_pc(c);
                c->pc = e;
            }
            return;
        case 0xe: c->x = arg(c, mode, &e); CLR_NZ(); NZ8(c->x); return;
        default: CLR_NZ(); NZ8(c->x); e = ea(c, mode); wm(c, e, c->x); return;   /* STX */
        }
    }
}

void m6805_init(M6805 *c)
{
    memset(c, 0, sizeof(*c));
    c->s = SP_MASK;
}

void m6805_reset(M6805 *c)
{
    c->s = SP_MASK;
    c->a = 0;
    c->x = 0;
    c->cc = IF;
    c->pending = 0;
    c->pc = rm16(c, VEC_RESET);
}

void m6805_set_irq(M6805 *c, int state)
{
    uint8_t s = (uint8_t)(state != 0);
    if (s != c->irq_pin) {
        c->irq_pin = s;
        if (s) c->pending |= M6805_INT_IRQ;
    }
}

int m6805_step(M6805 *c)
{
    uint8_t op;
    int cyc;
    if (c->pending && !(c->cc & IF)) {
        interrupt(c);
        c->total += 11;
        return 11;
    }
    c->ppc = c->pc;
    if (c->hook) c->hook(c);
    op = fetch(c);
    exec(c, op);
    cyc = cycles[op];
    c->total += (uint32_t)cyc;
    return cyc;
}
