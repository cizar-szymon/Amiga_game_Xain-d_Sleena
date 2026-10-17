/* Translation of 6809 code into 68k code at run time (from the user's ROMs; nothing of the game
 * is in the program).
 *
 * The assembler core (m6809_68k.s, tools/gen_6809_68k.py) contains, besides its handlers, the
 * pieces the translator puts together. Every instruction of the range becomes one of:
 *
 *  1. specialized code (_m6809_tq, _m6809_tqea): the handler of the core without reading its
 *     operands from the code (the translator writes them into holes), the address as a separate
 *     piece (direct, extended, one per indexed postbyte), then
 *         subq.l #cycles,d7 / ble.w cold     (cold: a4 = the next instruction, exit_loop)
 *     a4 (the PC) is not kept up to date in this code; it only has to point into the same ROM
 *     (wr_slow maps it again after an I/O write);
 *  2. a branch (Bcc, LBcc, BRA, LBRA) to an instruction of the range: a direct jump to its code
 *     (the core's idle loop check is not needed: targets in the idle map are left to case 3);
 *  3. the template of translation 1 (_m6809_tpl): movea.l #address after the opcode,a4,
 *     move.l #opcode,d0, the whole handler (it reads its operands through a4, keeps the PC,
 *     ends with jmp tr_next when it sets PC): JSR, RTS, PSHS, ...;
 *  4. one step of the C core (c_step: then check_ints, dispatch and back into the translation):
 *     instructions without a template (TFR, EXG, ANDCC, PULS CC, rare ones).
 *
 * The cycles are subtracted after each instruction as the core does, and the code leaves when
 * they are used up at the same instruction boundary, so the result is the same as the core's
 * (tests/lockstep.c TR). The map cpu->trmap (PC 8000-FFFF -> code of the instruction) is used
 * by the core's dispatch (slice start, check_ints, c_step, idle loops) and by tr_next.
 */
#include <string.h>
#include <stdlib.h>
#include "m6809.h"

uint32_t m6809_tr_count, m6809_tr_missing, m6809_tr_bytes;
uint32_t m6809_tr_kinds[4];             /* instructions per case 1-4 (statistics) */

/* length of the instruction at p (prefixes and indexed postbytes included); 0 = undefined */
static int idx_extra(uint8_t post)
{
    if (!(post & 0x80)) return 0;
    switch (post & 0x0f) {
    case 0x08: case 0x0c: return 1;
    case 0x09: case 0x0d: case 0x0f: return 2;
    default: return 0;
    }
}

static int insn_len(const uint8_t *p)
{
    uint8_t op = p[0], hi = (uint8_t)(op >> 4);
    if (op == 0x10 || op == 0x11) {
        uint8_t o2 = p[1], h2 = (uint8_t)(o2 >> 4), lo = (uint8_t)(o2 & 0x0f);
        if (op == 0x10 && h2 == 2) return 4;                      /* LBcc */
        if (o2 == 0x3f) return 2;                                 /* SWI2 / SWI3 */
        if (op == 0x10) {
            if (!(lo == 3 || lo == 0xc || lo == 0xe || lo == 0xf) || h2 < 8) return 0;
            if (h2 == 8 && lo == 0xf) return 0;
            if (h2 == 0xc && lo != 0xe) return 0;
            if (h2 >= 0xc && lo != 0xe && lo != 0xf) return 0;
        } else {
            if (!(lo == 3 || lo == 0xc) || h2 < 8 || h2 > 0xb) return 0;
        }
        switch (h2 & 3) {
        case 0: return 4;                                         /* immediate 16 bit */
        case 1: return 3;                                         /* direct */
        case 2: return 3 + idx_extra(p[2]);                       /* indexed */
        default: return 4;                                        /* extended */
        }
    }
    switch (hi) {
    case 0x0: return 2;
    case 0x1:
        switch (op) {
        case 0x12: case 0x13: case 0x19: case 0x1d: return 1;
        case 0x16: case 0x17: return 3;
        case 0x1a: case 0x1c: case 0x1e: case 0x1f: return 2;
        default: return 0;
        }
    case 0x2: return 2;
    case 0x3:
        if (op <= 0x33) return 2 + idx_extra(p[1]);
        if (op <= 0x37 || op == 0x3c) return 2;
        if (op == 0x38 || op == 0x3e) return 0;
        return 1;
    case 0x4: case 0x5: return 1;
    case 0x6: return 2 + idx_extra(p[1]);
    case 0x7: return 3;
    default:
        switch (hi & 3) {
        case 0:                                                   /* immediate */
            if (op == 0x87 || op == 0xc7 || op == 0x8f || op == 0xcf || op == 0xcd) return 0;
            return (op == 0x83 || op == 0x8c || op == 0x8e || op == 0xc3 || op == 0xcc || op == 0xce) ? 3 : 2;
        case 1: return 2;
        case 2: return 2 + idx_extra(p[1]);
        default: return 3;
        }
    }
}

#ifdef __VBCC__
#include <exec/memory.h>
#include <proto/exec.h>

extern const uint32_t m6809_tpl[3 * 256 * 3];   /* translation 1: start, end, flow */
extern const uint32_t m6809_tq[3 * 256 * 6];    /* specialized: pre, pre end, post, post end, info, holes */
extern const uint32_t m6809_tqea[258 * 4];      /* address pieces: direct, extended, indexed 00-FF */
extern char m6809_tr_next[], m6809_dispatch_plain[], m6809_exit_loop[], m6809_c_step[];

enum { K_SPEC, K_BRANCH, K_TPL, K_STEP };

typedef struct {
    uint16_t pc, len;
    uint8_t kind, page, op, hdr;
    int32_t target;                 /* branches: index of the target instruction */
    uint32_t hot, cold;             /* offsets of the code and of the cold stub */
    const uint32_t *row, *ea;       /* specialized: template row, address piece */
} Insn;

static const uint8_t *host(const M6809 *c, uint16_t pc) { return c->rbase[pc >> 8] + pc; }

/* the code writer: in the size pass (base NULL) only counts */
typedef struct { uint8_t *base; uint32_t pos; int bad; } Out;

static void w8(Out *o, uint32_t v) { if (o->base) o->base[o->pos] = (uint8_t)v; o->pos++; }
static void w16(Out *o, uint32_t v) { w8(o, v >> 8); w8(o, v); }
static void w32(Out *o, uint32_t v) { w16(o, v >> 16); w16(o, v & 0xffff); }
static void wbytes(Out *o, uint32_t from, uint32_t to)
{
    if (o->base) memcpy(o->base + o->pos, (const void *)from, to - from);
    o->pos += to - from;
}
/* 16 bit branch displacement from the extension word at o->pos to target offset t */
static void wdisp(Out *o, uint32_t t)
{
    int32_t d = (int32_t)t - (int32_t)o->pos;
    if (d < -32768 || d > 32767) o->bad = 1;
    w16(o, (uint32_t)d & 0xffff);
}
static void movea_a4(Out *o, uint32_t v) { w16(o, 0x287c); w32(o, v); }       /* movea.l #v,a4 */
static void jmp_abs(Out *o, const void *f) { w16(o, 0x4ef9); w32(o, (uint32_t)f); }
static void sub_d7(Out *o, int n)
{
    if (n >= 1 && n <= 8) w16(o, 0x5187 | (uint32_t)(n & 7) << 9);           /* subq.l #n,d7 */
    else { w16(o, 0x0487); w32(o, (uint32_t)n); }                             /* subi.l #n,d7 */
}

/* fill the holes of a piece copied at offset at (from template address base) with operand bytes */
static void holes(Out *o, uint32_t at, uint32_t hw, int count, int part, const uint8_t **opd)
{
    int i;
    for (i = 0; i < count && i < 2; i++) {
        uint32_t h = i ? hw & 0xffff : hw >> 16;
        uint32_t off;
        if ((int)(h >> 15) != part) continue;
        off = h & 0x3fff;
        if (h & 0x4000) {                                    /* word: placeholder A5A5 at +2 */
            if (o->base) {
                if (o->base[at + off + 2] != 0xa5 || o->base[at + off + 3] != 0xa5) o->bad = 1;
                o->base[at + off + 2] = (*opd)[0];
                o->base[at + off + 3] = (*opd)[1];
            }
            *opd += 2;
        } else {                                             /* byte: placeholder A5 at +3 */
            if (o->base) {
                if (o->base[at + off + 3] != 0xa5) o->bad = 1;
                o->base[at + off + 3] = (*opd)[0];
            }
            *opd += 1;
        }
    }
}

static int cycles_of(const Insn *in)
{
    int n = (int)(in->row[4] & 0xff);
    if (((in->row[4] >> 8) & 0xf) == 2) n += (int)(in->ea[2] & 0xff);
    return n;
}

/* hot code of one instruction */
static void emit_hot(Out *o, const M6809 *c, Insn *v, int i, int n)
{
    Insn *in = &v[i];
    const uint8_t *p = host(c, in->pc);
    in->hot = o->pos;
    switch (in->kind) {
    case K_SPEC: {
        const uint32_t *r = in->row;
        const uint8_t *opd = p + in->hdr + (((r[4] >> 8) & 0xf) == 2 ? 1 : 0);
        uint32_t at;
        at = o->pos; wbytes(o, r[0], r[1]); holes(o, at, r[5], (int)(r[4] >> 12 & 15), 0, &opd);   /* (before the address) */
        if (in->ea) { at = o->pos; wbytes(o, in->ea[0], in->ea[1]); holes(o, at, in->ea[3], (int)(in->ea[2] >> 12 & 15), 0, &opd); }
        at = o->pos; wbytes(o, r[2], r[3]); holes(o, at, r[5], (int)(r[4] >> 12 & 15), 1, &opd);
        sub_d7(o, cycles_of(in));
        w16(o, 0x6f00); wdisp(o, in->cold);                                   /* ble.w cold */
        break;
    }
    case K_BRANCH: {
        uint32_t t = v[in->target].hot;
        int cc = in->page ? (in->op & 15) : (in->op == 0x16 ? 0 : in->op & 15);
        int lng = in->page || in->op == 0x16;
        if (!lng) {
            sub_d7(o, 3);
            w16(o, 0x6f00); wdisp(o, in->cold);
            if (cc == 0) { w16(o, 0x6000); wdisp(o, t); }                     /* BRA */
            else if (cc != 1) { w16(o, 0x44c3); w16(o, 0x6000 | cc << 8); wdisp(o, t); }
        } else if (cc == 0) {                                                 /* LBRA (16, 1020) */
            sub_d7(o, in->page ? 6 : 5);
            w16(o, 0x6f00); wdisp(o, in->cold);
            w16(o, 0x6000); wdisp(o, t);
        } else {                                                              /* LBcc */
            sub_d7(o, 5);
            w16(o, 0x6f00); wdisp(o, in->cold);
            if (cc != 1) {
                w16(o, 0x44c3);                                               /* move.w d3,ccr */
                w16(o, 0x6000 | (cc ^ 1) << 8 | 10);                          /* not taken: skip */
                sub_d7(o, 1);
                w16(o, 0x6f00); wdisp(o, in->cold + 18);                      /* taken, used up */
                w16(o, 0x6000); wdisp(o, t);
            }
        }
        break;
    }
    case K_TPL: {
        const uint32_t *t = &m6809_tpl[(in->page * 256 + in->op) * 3];
        movea_a4(o, (uint32_t)(p + in->hdr));
        w16(o, 0x203c); w32(o, in->op);                                       /* move.l #op,d0 */
        wbytes(o, t[0], t[1]);
        break;
    }
    default:
        movea_a4(o, (uint32_t)p);
        jmp_abs(o, m6809_c_step);
        break;
    }
    (void)n;
}

/* cold stubs: leave with a4 at the right instruction */
static void emit_cold(Out *o, const M6809 *c, Insn *v, int i)
{
    Insn *in = &v[i];
    uint32_t next = (uint32_t)host(c, (uint16_t)(in->pc + in->len));
    in->cold = o->pos;
    if (in->kind == K_SPEC) {
        movea_a4(o, next); jmp_abs(o, m6809_exit_loop);
    } else if (in->kind == K_BRANCH) {
        uint32_t tgt = (uint32_t)host(c, v[in->target].pc);
        int cc = in->page ? (in->op & 15) : (in->op == 0x16 ? 0 : in->op & 15);
        int lng = in->page || in->op == 0x16;
        if (cc == 0) { movea_a4(o, tgt); jmp_abs(o, m6809_exit_loop); }
        else if (cc == 1) { movea_a4(o, next); jmp_abs(o, m6809_exit_loop); }
        else if (!lng) {
            w16(o, 0x44c3); w16(o, 0x6000 | cc << 8 | 12);                    /* taken: skip 12 */
            movea_a4(o, next); jmp_abs(o, m6809_exit_loop);
            movea_a4(o, tgt); jmp_abs(o, m6809_exit_loop);
        } else {
            /* used up after 5 cycles: not taken -> next; taken -> one more cycle, target */
            w16(o, 0x44c3); w16(o, 0x6000 | cc << 8 | 12);
            movea_a4(o, next); jmp_abs(o, m6809_exit_loop);
            sub_d7(o, 1);                                                    /* cold + 16 */
            movea_a4(o, tgt); jmp_abs(o, m6809_exit_loop);                   /* cold + 18 */
        }
    }
}

/* one pass: offsets (base NULL) or the code */
static uint32_t layout(Out *o, const M6809 *c, Insn *v, int n, uint16_t to)
{
    int i;
    for (i = 0; i < n; i++)
        emit_hot(o, c, v, i, n);
    /* after the last instruction: the map or the core (a4 may be stale after specialized code) */
    movea_a4(o, (uint32_t)host(c, to));
    jmp_abs(o, m6809_tr_next);
    for (i = 0; i < n; i++)
        emit_cold(o, c, v, i);
    return o->pos;
}

int m6809_tr_build(M6809 *c, uint16_t from, uint16_t to)
{
    void **map;
    uint8_t *code;
    Insn *v;
    Out o;
    int n = 0, i, max = to - from;
    uint32_t pc, size, p;
    static int32_t index[0x8000];
    if (from < 0x8000 || to <= from || !m6809_layout_ok()) return 0;
    for (p = from >> 8; p <= (uint32_t)((to - 1) >> 8); p++)
        if (!c->rpage[p] || c->rbase[p] != c->rbase[0x80]) return 0;     /* ROM in one piece */
    m6809_tr_free(c);
    v = (Insn *)malloc(sizeof(Insn) * (size_t)max);
    if (!v) return 0;
    /* decode */
    for (i = 0; i < 0x8000; i++) index[i] = -1;
    for (pc = from; pc < to; ) {
        const uint8_t *q = host(c, (uint16_t)pc);
        int len = insn_len(q);
        Insn *in = &v[n];
        memset(in, 0, sizeof *in);
        in->pc = (uint16_t)pc;
        in->len = (uint16_t)(len ? len : 1);
        in->hdr = 1;
        in->op = q[0];
        if (q[0] == 0x10 || q[0] == 0x11) { in->page = q[0] == 0x10 ? 1 : 2; in->op = q[1]; in->hdr = 2; }
        in->kind = len ? K_TPL : K_STEP;
        index[pc - 0x8000] = n;
        n++;
        if (!len) break;
        pc += (uint32_t)len;
    }
    /* choose the case of each instruction */
    m6809_tr_kinds[0] = m6809_tr_kinds[1] = m6809_tr_kinds[2] = m6809_tr_kinds[3] = 0;
    for (i = 0; i < n; i++) {
        Insn *in = &v[i];
        const uint8_t *q = host(c, in->pc);
        int isbr = (in->page == 0 && ((in->op & 0xf0) == 0x20 || in->op == 0x16)) ||
                   (in->page == 1 && (in->op & 0xf0) == 0x20);
        if (in->kind == K_STEP) { m6809_tr_kinds[3]++; continue; }
        if (isbr) {
            int32_t d = (in->len == 2) ? (int8_t)q[1] : (int16_t)(q[in->len - 2] << 8 | q[in->len - 1]);
            uint32_t t = (uint16_t)(in->pc + in->len + d);
            if (t >= from && t < to && index[t - 0x8000] >= 0 && !(c->idle_map[t >> 3] & (1 << (t & 7)))) {
                in->kind = K_BRANCH;
                in->target = index[t - 0x8000];
                m6809_tr_kinds[1]++;
                continue;
            }
        } else {
            const uint32_t *r = &m6809_tq[(in->page * 256 + in->op) * 6];
            if (r[0]) {
                int ea = (int)((r[4] >> 8) & 0xf);
                const uint32_t *e = 0;
                if (ea == 1) e = &m6809_tqea[0];
                else if (ea == 3) e = &m6809_tqea[4];
                else if (ea == 2) e = &m6809_tqea[(2 + q[in->hdr]) * 4];
                if (!ea || e[0]) {
                    in->kind = K_SPEC;
                    in->row = r;
                    in->ea = e;
                    m6809_tr_kinds[0]++;
                    continue;
                }
            }
        }
        if (m6809_tpl[(in->page * 256 + in->op) * 3]) { in->kind = K_TPL; m6809_tr_kinds[2]++; }
        else { in->kind = K_STEP; m6809_tr_kinds[3]++; }
    }
    /* two passes: offsets, then the code */
    memset(&o, 0, sizeof o);
    size = layout(&o, c, v, n, (uint16_t)(v[n - 1].pc + v[n - 1].len));
    map = (void **)AllocVec(0x8000 * sizeof(void *), MEMF_ANY | MEMF_CLEAR);
    code = (uint8_t *)AllocVec(size + 4, MEMF_ANY);
    if (!map || !code || o.bad) {
        if (map) FreeVec(map);
        if (code) FreeVec(code);
        free(v);
        return 0;
    }
    ((uint32_t *)code)[0] = size;
    o.base = code + 4;
    o.pos = 0;
    layout(&o, c, v, n, (uint16_t)(v[n - 1].pc + v[n - 1].len));
    if (o.bad) {                        /* a hole without its placeholder or a branch too far */
        FreeVec(map);
        FreeVec(code);
        free(v);
        return 0;
    }
    for (i = 0; i < n; i++)
        map[v[i].pc - 0x8000] = code + 4 + v[i].hot;
    m6809_tr_count = (uint32_t)n;
    m6809_tr_missing = m6809_tr_kinds[3];
    m6809_tr_bytes = size;
    free(v);
    CacheClearU();
    c->trmap = map;
    return 1;
}

void m6809_tr_free(M6809 *c)
{
    if (c->trmap) {
        uint32_t i;
        for (i = 0; i < 0x8000; i++)                /* the code: the first instruction's code */
            if (c->trmap[i]) { FreeVec((uint8_t *)c->trmap[i] - 4); break; }
        FreeVec(c->trmap);
        c->trmap = 0;
    }
}

#else
/* no 68k: the translation is only for the assembler core */
int m6809_tr_build(M6809 *c, uint16_t from, uint16_t to) { (void)c; (void)from; (void)to; (void)insn_len; return 0; }
void m6809_tr_free(M6809 *c) { c->trmap = 0; }
#endif
