/* Xain'd Sleena machine model, see xain.h. Memory maps, interrupts and timing follow MAME 0.289
 * (doc/mame0289/xain.cpp): main_map, cpu_map_B, sound_map, scanline().
 */
#include "xain.h"
#include "romload.h"
#include <string.h>

/* ---- main CPU (CPU A) ---- */

static void main_set_bank(Xain *x)
{
    int i;
    for (i = 0x40; i < 0x80; i++)
        m6809_map_page(&x->main, i, x->main_bank_rom[x->main_bank] + (i - 0x40) * 256, x->ram_sink);
}

/* 3A05: bits 0-1, 6-7 unused (1), bit 2 coin 3 (active low), bits 3-4 MCU flags, bit 5 VBLANK */
static uint8_t status_3a05(const Xain *x)
{
    return (uint8_t)(0xc3 | (x->coin3 ? 0 : 0x04) | mcu_host_status(&x->mcu) | (x->vblank ? 0x20 : 0));
}

/* the value of 3A05 for the assembler core (M6809.fast_val): it changes only between the time
   slices of the main CPU and by its own I/O accesses (MCU), after which this runs again */
static void main_fast_update(Xain *x)
{
    x->main.fast_addr = 0x3a05;
    x->main.fast_val = status_3a05(x);
    x->main.fast_ok = 1;
}

static uint8_t main_read_io(Xain *x, uint16_t a)
{
    if (a >= 0x3900 && a < 0x3980) return x->spriteram[a - 0x3800];
    switch (a) {
    case 0x3a00: return (uint8_t)~x->in_p1;
    case 0x3a01: return (uint8_t)~x->in_p2;
    case 0x3a02: return x->dsw0;
    case 0x3a03: return x->dsw1;
    case 0x3a04: return mcu_host_read(&x->mcu);
    case 0x3a05:
        return status_3a05(x);
    case 0x3a06:
        mcu_host_reset(&x->mcu);
        return 0xff;
    default:
        return 0x00;                                    /* unmapped, palette (write only) */
    }
}

static uint8_t main_read(void *ctx, uint16_t a)
{
    Xain *x = ctx;
    uint8_t v = main_read_io(x, a);
    main_fast_update(x);                                /* 3A04 / 3A06 change the MCU flags */
    return v;
}

static void main_write_io(Xain *x, uint16_t a, uint8_t v);

static void main_write(void *ctx, uint16_t a, uint8_t v)
{
    Xain *x = ctx;
    main_write_io(x, a, v);
    main_fast_update(x);                                /* 3A0E changes the MCU flags */
}

static void main_write_io(Xain *x, uint16_t a, uint8_t v)
{
    if (a >= 0x3900 && a < 0x3980) { x->spriteram[a - 0x3800] = v; return; }
    if (a >= 0x3c00 && a < 0x4000) {
        if (a < 0x3e00) x->palette[a - 0x3c00] = v;
        else x->palette_ext[a - 0x3e00] = v;
        x->palette_dirty = 1;
        return;
    }
    switch (a) {
    case 0x3a00: case 0x3a01: x->scrollx[1][a & 1] = v; break;
    case 0x3a02: case 0x3a03: x->scrolly[1][a & 1] = v; break;
    case 0x3a04: case 0x3a05: x->scrollx[0][a & 1] = v; break;
    case 0x3a06: case 0x3a07: x->scrolly[0][a & 1] = v; break;
    case 0x3a08:
        x->soundlatch = v;
        x->snd.irq_line = 1;                            /* data pending -> sound IRQ */
        if (x->sound_cmd) x->sound_cmd(x->sound_ctx, v);
        break;
    case 0x3a09: m6809_set_nmi(&x->main, 0); break;
    case 0x3a0a: x->main.firq_line = 0; break;
    case 0x3a0b: x->main.irq_line = 0; break;
    case 0x3a0c: x->sub.irq_line = 1; break;
    case 0x3a0d: x->flip = v & 1; break;
    case 0x3a0e: mcu_host_write(&x->mcu, v); break;
    case 0x3a0f:
        x->pri = v & 7;
        x->main_bank = (v >> 3) & 1;
        main_set_bank(x);
        break;
    default: break;
    }
}

/* ---- sub CPU (CPU B) ---- */

static void sub_set_bank(Xain *x)
{
    int i;
    for (i = 0x40; i < 0x80; i++)
        m6809_map_page(&x->sub, i, x->sub_bank_rom[x->sub_bank] + (i - 0x40) * 256, x->ram_sink);
}

static uint8_t sub_read(void *ctx, uint16_t a)
{
    (void)ctx;
    (void)a;
    return 0x00;
}

static void sub_write(void *ctx, uint16_t a, uint8_t v)
{
    Xain *x = ctx;
    if (a == 0x2000) x->main.irq_line = 1;
    else if (a == 0x2800) x->sub.irq_line = 0;
    else if (a == 0x3000) { x->sub_bank = v & 1; sub_set_bank(x); }
}

/* ---- sound CPU ---- */

static uint8_t snd_read(void *ctx, uint16_t a)
{
    Xain *x = ctx;
    if (a == 0x1000) {
        x->snd.irq_line = 0;                            /* reading the latch acknowledges it */
        return x->soundlatch;
    }
    return 0x00;
}

static void snd_write(void *ctx, uint16_t a, uint8_t v)
{
    Xain *x = ctx;
    if (a == 0x2800 || a == 0x2801) ym2203_write(&x->ym[0], a & 1, v);
    else if (a == 0x3000 || a == 0x3001) ym2203_write(&x->ym[1], a & 1, v);
}

/* ---- setup ---- */

static void map_pages(Xain *x)
{
    int i;
    M6809 *m = &x->main, *s = &x->sub, *a = &x->snd;
    for (i = 0; i < 256; i++) {
        m6809_map_page(m, i, 0, 0);
        m6809_map_page(s, i, 0, 0);
        m6809_map_page(a, i, 0, 0);
    }
    for (i = 0x00; i < 0x20; i++) {
        m6809_map_page(m, i, x->shared + i * 256, x->shared + i * 256);
        m6809_map_page(s, i, x->shared + i * 256, x->shared + i * 256);
    }
    for (i = 0x20; i < 0x28; i++) m6809_map_page(m, i, x->charram + (i - 0x20) * 256, x->charram + (i - 0x20) * 256);
    for (i = 0x28; i < 0x30; i++) m6809_map_page(m, i, x->bgram1 + (i - 0x28) * 256, x->bgram1 + (i - 0x28) * 256);
    for (i = 0x30; i < 0x38; i++) m6809_map_page(m, i, x->bgram0 + (i - 0x30) * 256, x->bgram0 + (i - 0x30) * 256);
    m6809_map_page(m, 0x38, x->spriteram, x->spriteram);
    for (i = 0x80; i < 0x100; i++) {
        m6809_map_page(m, i, x->main_rom + (i - 0x80) * 256, x->ram_sink);
        m6809_map_page(s, i, x->sub_rom + (i - 0x80) * 256, x->ram_sink);
        m6809_map_page(a, i, x->snd_rom + (i - 0x80) * 256, x->ram_sink);
    }
    for (i = 0x40; i < 0x80; i++) {
        m6809_map_page(m, i, 0, x->ram_sink);           /* reads: the banks (main/sub_set_bank) */
        m6809_map_page(s, i, 0, x->ram_sink);
        m6809_map_page(a, i, x->zero_page, x->ram_sink);   /* sound ROM region 4000-7FFF is empty */
    }
    for (i = 0x00; i < 0x08; i++) m6809_map_page(a, i, x->snd_ram + i * 256, x->snd_ram + i * 256);
    main_set_bank(x);
    sub_set_bank(x);
}

void xain_init(Xain *x)
{
    m6809_init(&x->main);
    m6809_init(&x->sub);
    m6809_init(&x->snd);
    x->main.read = main_read; x->main.write = main_write; x->main.ctx = x;
    x->sub.read = sub_read; x->sub.write = sub_write; x->sub.ctx = x;
    x->snd.read = snd_read; x->snd.write = snd_write; x->snd.ctx = x;
    ym2203_init(&x->ym[0]);
    ym2203_init(&x->ym[1]);
    xain_set_sound(x, 1);
    x->dsw0 = 0x3f;                                     /* MAME defaults (also the MiSTer .mra) */
    x->dsw1 = 0xff;
    x->cpu_run = m6809_run;
    x->slice = 96;                                      /* one scanline; see docs/PORTING.md */
    x->idle_skip = 1;
    x->slice_memo = 1;
    x->sub_fast = 1;
    x->hle_sprites = 0;                                 /* slower than the asm core on the 68060 */
    x->sound_cpu = 1;
    xain_reset(x);
}

/* pure polling loops (found in MAME traces of the demo and play1, see docs/PORTING.md) */
static const uint8_t idle_main_8727[] = { 0x96, 0x3e, 0x84, 0x01, 0x27, 0xfa };              /* LDA <$3E ANDA #1 BEQ */
static const uint8_t idle_main_8749[] = { 0xb7, 0x3a, 0x09, 0x96, 0x3d, 0x94, 0x3e, 0x84, 0x02, 0x27, 0xf5 }; /* STA $3A09 ... BEQ */
static const uint8_t idle_main_a25d[] = { 0xf6, 0x3a, 0x05, 0xc4, 0x20, 0x26, 0xf9 };        /* LDB $3A05 ANDB #$20 BNE */
static const uint8_t idle_main_a268[] = { 0xf6, 0x3a, 0x05, 0xc4, 0x20, 0x27, 0xf9 };        /* LDB $3A05 ANDB #$20 BEQ */
static const uint8_t idle_sub_8014[] = { 0x7e, 0x80, 0x14 };                                  /* JMP $8014 */
static const uint8_t idle_sub_4b91[] = { 0x96, 0x3d, 0x85, 0x08, 0x27, 0xfa };                /* LDA <$3D BITA #8 BEQ (bank 0) */

static void setup_idle(Xain *x)
{
    x->main.nidle = x->sub.nidle = x->snd.nidle = 0;
    x->mcu.idle = x->idle_skip;
    if (!x->idle_skip)
        return;
    m6809_add_idle_loop(&x->main, 0x8727, idle_main_8727, sizeof idle_main_8727);
    m6809_add_idle_loop(&x->main, 0x8749, idle_main_8749, sizeof idle_main_8749);
    m6809_add_idle_loop(&x->main, 0xa25d, idle_main_a25d, sizeof idle_main_a25d);
    m6809_add_idle_loop(&x->main, 0xa268, idle_main_a268, sizeof idle_main_a268);
    m6809_add_idle_loop(&x->sub, 0x8014, idle_sub_8014, sizeof idle_sub_8014);
    m6809_add_idle_loop(&x->sub, 0x4b91, idle_sub_4b91, sizeof idle_sub_4b91);
}

void xain_reset(Xain *x)
{
    memset(x->shared, 0, sizeof x->shared);
    memset(x->snd_ram, 0, sizeof x->snd_ram);
    memset(x->scrollx, 0, sizeof x->scrollx);
    memset(x->scrolly, 0, sizeof x->scrolly);
    x->pri = x->main_bank = x->sub_bank = x->flip = 0;
    x->soundlatch = 0;
    memset(x->memo, 0, sizeof x->memo);
    map_pages(x);
    x->main.irq_line = x->main.firq_line = 0;
    x->sub.irq_line = x->sub.firq_line = 0;
    x->snd.irq_line = x->snd.firq_line = 0;
    m6809_set_nmi(&x->main, 0);
    m6809_reset(&x->main);
    m6809_reset(&x->sub);
    m6809_reset(&x->snd);
    x->main.icount = x->sub.icount = x->snd.icount = 0;
    mcu_init(&x->mcu, x->mcu_rom, x->mcu_boot);
    setup_idle(x);
    ym2203_reset(&x->ym[0]);
    ym2203_reset(&x->ym[1]);
    x->line = 0;
    x->frame = 0;
    x->vblank = 0;
    x->palette_dirty = 1;
}

int xain_load_roms(Xain *x, const char *const *sources, int nsources)
{
    static uint8_t main_banked[0x8000], sub_banked[0x8000];
    RomRequest req[32];
    int n = 0, missing;
#define REQ(nm_, sz_, crc_, dst_) do { req[n].name = nm_; req[n].size = sz_; req[n].crc = crc_; \
        req[n].dest = dst_; req[n].found = 0; n++; } while (0)
    REQ("p9-08.ic66", 0x8000, 0x5179ae3fUL, x->main_rom);
    REQ("pa-09.ic65", 0x8000, 0x10a7c800UL, main_banked);
    REQ("p1-0.ic29", 0x8000, 0xa1a860e2UL, x->sub_rom);
    REQ("p0-0.ic15", 0x8000, 0x948b9757UL, sub_banked);
    REQ("p2-0.ic49", 0x8000, 0xa5318cb8UL, x->snd_rom);
    REQ("pz-0.113", 0x800, 0xa432a907UL, x->mcu_rom);
    REQ("pb-0.ic24|pb-01.ic24", 0x8000, 0x83c00dd8UL, x->gfx_chars);
    REQ("pk-0.ic136", 0x8000, 0x11eb4247UL, x->gfx_bg1 + 0x00000);
    REQ("pl-0.ic135", 0x8000, 0x422b536eUL, x->gfx_bg1 + 0x08000);
    REQ("pm-0.ic134", 0x8000, 0x828c1b0cUL, x->gfx_bg1 + 0x10000);
    REQ("pn-0.ic133", 0x8000, 0xd37939e0UL, x->gfx_bg1 + 0x18000);
    REQ("pc-0.ic114", 0x8000, 0x8f0aa1a7UL, x->gfx_bg1 + 0x20000);
    REQ("pd-0.ic113", 0x8000, 0x45681910UL, x->gfx_bg1 + 0x28000);
    REQ("pe-0.ic112", 0x8000, 0xa8eeabc8UL, x->gfx_bg1 + 0x30000);
    REQ("pf-0.ic111", 0x8000, 0xe59a2f27UL, x->gfx_bg1 + 0x38000);
    REQ("p5-0.ic44", 0x8000, 0x5c6c453cUL, x->gfx_bg2 + 0x00000);
    REQ("p4-0.ic45", 0x8000, 0x59d87a9aUL, x->gfx_bg2 + 0x08000);
    REQ("p3-0.ic46", 0x8000, 0x84884a2eUL, x->gfx_bg2 + 0x10000);
    REQ("p6-0.ic43", 0x8000, 0x8d637639UL, x->gfx_bg2 + 0x20000);
    REQ("p7-0.ic42", 0x8000, 0x71eec4e6UL, x->gfx_bg2 + 0x28000);
    REQ("p8-0.ic41", 0x8000, 0x7fc9704fUL, x->gfx_bg2 + 0x30000);
    REQ("po-0.ic131", 0x8000, 0x252976aeUL, x->gfx_obj + 0x00000);
    REQ("pp-0.ic130", 0x8000, 0xe6f1e8d5UL, x->gfx_obj + 0x08000);
    REQ("pq-0.ic129", 0x8000, 0x785381edUL, x->gfx_obj + 0x10000);
    REQ("pr-0.ic128", 0x8000, 0x59754e3dUL, x->gfx_obj + 0x18000);
    REQ("pg-0.ic109", 0x8000, 0x4d977f33UL, x->gfx_obj + 0x20000);
    REQ("ph-0.ic108", 0x8000, 0x3f3b62a0UL, x->gfx_obj + 0x28000);
    REQ("pi-0.ic107", 0x8000, 0x76641ee3UL, x->gfx_obj + 0x30000);
    REQ("pj-0.ic106", 0x8000, 0x37671f36UL, x->gfx_obj + 0x38000);
    REQ("pt-0.ic59", 0x100, 0xfed32888UL, x->prom);
#undef REQ
    missing = rom_load(sources, nsources, req, n);
    /* pa-09 / p0-0: first half = bank 0 at 4000, second half = bank 1 (MAME region 0x10000) */
    memcpy(x->main_bank_rom[0], main_banked, 0x4000);
    memcpy(x->main_bank_rom[1], main_banked + 0x4000, 0x4000);
    memcpy(x->sub_bank_rom[0], sub_banked, 0x4000);
    memcpy(x->sub_bank_rom[1], sub_banked + 0x4000, 0x4000);
    /* the 68705P5 bootstrap ROM is optional (the game does not use it) */
    {
        RomRequest boot;
        boot.name = "bootstrap.bin";
        boot.size = 0x73;
        boot.crc = 0xf70a8620UL;
        boot.dest = x->mcu_boot;
        boot.found = 0;
        if (rom_load_quiet(sources, nsources, &boot, 1)) memset(x->mcu_boot, 0, sizeof x->mcu_boot);
    }
    return missing;
}

/* ---- timing ---- */

static int vcount_of(int y)
{
    int v = y + 8;
    return v < 0x100 ? v : ((v - 0x18) | 0x100);
}

/* profiling (STATS=FULL, tests): called after each part of a slice with its number */
void (*xain_prof)(int part);

/*
 * The main CPU waits most of the frame in two loops of its main program:
 *   8727 LDA <$3E / ANDA #1 / BEQ 8727                 (until bit 0 of $3E)
 *   8749 STA $3A09 / LDA <$3D / ANDA <$3E / ANDA #2 / BEQ 8749   (until bit 1 of $3D & $3E)
 * The idle loop skipping of the cores still runs one or two turns per time slice (one scanline)
 * to recognise the loop. When a slice starts inside one of these loops, the loop cannot be left
 * during it (the condition is false, no interrupt is pending, nothing else runs during the main
 * CPU's slice), so its result depends only on PC, A, CC, the cycles carried over, $3D, $3E and
 * the NMI line (3A09 clears it): such slices are run once and then replayed from a small table.
 * Exact by construction; build/probe/memocheck.c compares with slice_memo = 0 (8000 frames of
 * play identical, 144 of 272 slices per frame replayed).
 */
static int main_in_wait(const Xain *x)
{
    const M6809 *c = &x->main;
    uint16_t pc = c->pc;
    if (c->dp || c->waiting || c->freerun || c->hook || c->nmi_pending ||
        (c->firq_line && !(c->cc & M6809_CC_F)) || (c->irq_line && !(c->cc & M6809_CC_I)))
        return 0;
    if (pc >= 0x8727 && pc < 0x872d)
        return !(x->shared[0x3e] & 1);
    if (pc >= 0x8749 && pc < 0x8754)
        return !(x->shared[0x3d] & x->shared[0x3e] & 2);
    return 0;
}

/*
 * The sub CPU waits for its IRQ in 8014 JMP $8014 most of the time (ca. 174 of 272 slices per
 * frame in play). A slice that starts there with no interrupt pending (nothing else runs during
 * the sub CPU's slice) only repeats the 4 cycle JMP until the cycles are used up: done here
 * without the core (A1200: 13 us per such slice through the asm core). Exact; sub_fast = 0 (Amiga
 * SUBFAST=OFF) runs them on the core, tools/memocheck.c compares.
 */
static void run_sub(Xain *x, int32_t cycles)
{
    M6809 *c = &x->sub;
    if (x->sub_fast && c->pc == 0x8014 && !c->hook && !c->waiting && !c->freerun && !c->nmi_pending &&
        !(c->firq_line && !(c->cc & M6809_CC_F)) && !(c->irq_line && !(c->cc & M6809_CC_I)) &&
        c->rpage[0x80] && c->rpage[0x80][0x14] == 0x7e && c->rpage[0x80][0x15] == 0x80 &&
        c->rpage[0x80][0x16] == 0x14) {
        c->icount += cycles;
        if (c->icount > 0) {
            int32_t n = (c->icount + 3) >> 2;              /* JMP ext: 4 cycles, while icount > 0 */
            c->icount -= n * 4;
            c->total += (uint32_t)(n * 4);
            c->idle_skipped += (uint32_t)(n * 4);
            c->ppc = 0x8014;
        }
        x->sub_fast_slices++;
        return;
    }
    x->cpu_run(c, cycles);
}

static void run_main(Xain *x, int32_t cycles)
{
    M6809 *c = &x->main;
    XainSliceMemo *e;
    uint32_t total0, skipped0;
    if (x->hle_sprites && c->pc >= 0xdcec && c->pc < 0xde89 && !c->hook && !c->waiting && !c->freerun &&
        !c->nmi_pending && !(c->firq_line && !(c->cc & M6809_CC_F)) && !(c->irq_line && !(c->cc & M6809_CC_I))) {
        /* in the sprite list routine: the translated code, the rest of the slice on the core */
        c->icount += cycles;
        m6809_run_spr(c);
        x->spr_slices++;
        if (c->icount > 0)
            x->cpu_run(c, 0);
        return;
    }
    if (!x->slice_memo || !main_in_wait(x)) {
        x->cpu_run(c, cycles);
        return;
    }
    e = &x->memo[(c->pc * 7u + (uint32_t)c->icount * 13u + c->a) & (XAIN_MEMO_SIZE - 1)];
    if (e->used && e->pc == c->pc && e->icount == c->icount && e->cycles == cycles && e->a == c->a && e->cc == c->cc &&
        e->m3d == x->shared[0x3d] && e->m3e == x->shared[0x3e] && e->nmi == c->nmi_line) {
        c->pc = e->pc2;
        c->ppc = e->ppc2;
        c->a = e->a2;
        c->cc = e->cc2;
        c->nmi_line = e->nmi2;
        c->icount = e->icount2;
        c->total += e->dtotal;
        c->idle_skipped += e->dskipped;
        x->memo_hits++;
        return;
    }
    e->used = 0;
    e->pc = c->pc; e->icount = c->icount; e->cycles = cycles; e->a = c->a; e->cc = c->cc;
    e->m3d = x->shared[0x3d]; e->m3e = x->shared[0x3e]; e->nmi = c->nmi_line;
    total0 = c->total;
    skipped0 = c->idle_skipped;
    x->cpu_run(c, cycles);
    x->memo_runs++;
    /* still in the loop with the same memory: the slice depends only on the key */
    if (main_in_wait(x) && x->shared[0x3d] == e->m3d && x->shared[0x3e] == e->m3e) {
        e->pc2 = c->pc; e->ppc2 = c->ppc; e->a2 = c->a; e->cc2 = c->cc; e->nmi2 = c->nmi_line;
        e->icount2 = c->icount;
        e->dtotal = c->total - total0;
        e->dskipped = c->idle_skipped - skipped0;
        e->used = 1;
    }
}

static void run_slice(Xain *x, int32_t cycles)
{
    if (xain_prof) {
        xain_prof(-1);
        main_fast_update(x);
        run_main(x, cycles);
        xain_prof(0);
        run_sub(x, cycles);
        xain_prof(1);
        if (x->sound_cpu) x->cpu_run(&x->snd, cycles);
        xain_prof(2);
        if (x->sound_cpu) {
            x->snd.firq_line = (uint8_t)ym2203_advance(&x->ym[0], cycles * 2);
            ym2203_advance(&x->ym[1], cycles * 2);
        }
        xain_prof(3);
        mcu_run(&x->mcu, cycles / 2);
        xain_prof(4);
        return;
    }
    main_fast_update(x);                                /* VBLANK, coin 3, MCU flags since the last slice */
    run_main(x, cycles);
    run_sub(x, cycles);
    if (x->sound_cpu) {
        /* the sound CPU only receives (sound latch), so it can be left out without sound */
        x->cpu_run(&x->snd, cycles);
        /* YM2203 at 3 MHz: 2 clocks per 6809 cycle; YM #1 IRQ -> sound FIRQ */
        x->snd.firq_line = (uint8_t)ym2203_advance(&x->ym[0], cycles * 2);
        ym2203_advance(&x->ym[1], cycles * 2);
    }
    mcu_run(&x->mcu, cycles / 2);                       /* 3 MHz / 4 = half the 6809 rate */
}

static void scanline(Xain *x, int y)
{
    int vold = vcount_of(y == 0 ? XAIN_LINES - 1 : y - 1);
    int v = vcount_of(y);
    /* MAME update_partial(y - 1): the previous line is drawn with the state at this point
       (the last visible line, 247, at the end of xain_run_frame) */
    if (y - 1 >= XAIN_FIRST_LINE && y - 1 < XAIN_FIRST_LINE + SCREEN_H - 1 && x->render_line)
        x->render_line(x->render_ctx, x, y - 1);
    if (!(vold & 8) && (v & 8)) x->main.firq_line = 1;  /* IMS every 16 lines */
    if (v == 0xf8) m6809_set_nmi(&x->main, 1);          /* VBLANK NMI, latched */
    x->vblank = (uint8_t)(v >= 248 - 1);                /* MAME raises VBLANK a line early */
}

/* A frame runs from the start of VBLANK (scanline 248) to the end of the visible area (247), as
   MAME's frame notifier: in MAME traces the NMI (scanline 240) comes 264 lines after power-on,
   so the machine starts at scanline 248. */
#define FRAME_START_LINE (XAIN_FIRST_LINE + SCREEN_H)

void xain_run_frame(Xain *x)
{
    int i, s;
    for (i = 0; i < XAIN_LINES; i++) {
        int y = (FRAME_START_LINE + i) % XAIN_LINES;
        x->line = y;
        scanline(x, y);
        for (s = 0; s < XAIN_LINE_CYCLES; s += x->slice)
            run_slice(x, x->slice);
    }
    /* VBLANK starts: the screen is complete up to line 247 (MAME draws it before the frame
       notifier, the same moment as the scanline 248 callback of the next frame) */
    if (x->render_line)
        x->render_line(x->render_ctx, x, FRAME_START_LINE - 1);
    x->frame++;
}

/* ---- sound ---- */

void xain_set_fm_div(Xain *x, int div)
{
    x->ym[0].fm_div = x->ym[1].fm_div = (uint8_t)(div == 2 ? 2 : 1);
    x->ym[0].div_count = x->ym[1].div_count = 0;
    x->audio_pos = x->ym[0].out_pos;
    x->audio_frac = 0;
}

/* ---- front end help (START=EASY; not part of the machine) ---- */

/* shared RAM: credits, 1 while credits or a game (the start screen), 1 while a game runs */
#define RAM_CREDITS 0x31
#define RAM_CREDIT_MODE 0x36
#define RAM_PLAYING 0x39

#define RAM_PLAYER 0x3B

/* the patched continue text (row 8, column 5) is on the screen */
static const char cont_text[] = "PRESS FIRE TO CONTINUE";

static int on_continue_screen(const Xain *x)
{
    const uint8_t *p = x->charram + 8 * 32 + 5;
    int i;
    for (i = 0; i < 10; i++)
        if (p[i] != (uint8_t)(cont_text[i] - 0x20))
            return 0;
    return 1;
}

void xain_assist(Xain *x)
{
    int playing = x->shared[RAM_PLAYING] != 0;
    if (x->autocoin) {
        /* a coin: 6 frames on the coin input, then 24 off (as tools/mame_capture.py play1) */
        if (x->coin_phase > 0) {
            if (x->coin_phase > 24)
                x->in_p2 |= IN_P2_COIN1;
            x->coin_phase--;
        } else if (!playing && x->shared[RAM_CREDITS] < 2 && x->frame > 60) {
            x->coin_phase = 30;
        }
    }
    if (x->joystart && !playing && x->shared[RAM_CREDIT_MODE]) {
        if (x->in_p1 & IN_LEFT) x->in_p1 |= IN_P1_START1;
        if (x->in_p1 & IN_RIGHT) x->in_p1 |= IN_P1_START2;
    }
    if (x->joystart) {
        /* the continue screen (main 840B-8467): waits for start 1 when <$3B bit 0 is set, else
           start 2 (both pressed do not count); a new fire press holds the right one */
        int fire = (x->in_p1 & IN_BUTTON1) != 0;
        if (on_continue_screen(x)) {
            /* no credit: a coin (not after the continue: the text stays a few frames) */
            if (!x->shared[RAM_CREDITS] && !x->coin_phase && x->autocoin && !x->cont_fire)
                x->coin_phase = 30;
            if (fire && !x->prev_fire)
                x->cont_fire = 1;
            if (x->cont_fire && fire)
                x->in_p1 |= (x->shared[RAM_PLAYER] & 1) ? IN_P1_START1 : IN_P1_START2;
        } else {
            x->cont_fire = 0;
        }
        x->prev_fire = fire;
    }
}

/* main 84DD-84F3 (stage start): <$53 bit 7 = the ship (sub 823D -> 82CC/8A75/8AAB: button 2 =
   object 7), clear = on foot (sub 8355: button 2 = jump); <$48 = the stage type, 0 on the planet
   map (sub bank 0 7D04: any button picks a planet) */
#define RAM_STAGE 0x48
#define RAM_SHIP 0x53

int xain_on_foot(const Xain *x)
{
    return x->shared[RAM_PLAYING] && x->shared[RAM_STAGE] && !(x->shared[RAM_SHIP] & 0x80);
}

int xain_patch_texts(Xain *x)
{
    /* pa-09.ic65 7885 (main bank 1 at 7885): colour 02, text address 2246 (row 18, column 6),
       20 characters (ASCII, shown as code - 0x20), end 03 */
    static const char old_text[] = "PUSH 1P OR 2P BUTTON";
    static const char new_text[] = "LEFT 1P OR RIGHT 2P ";
    /* continue screen (main 840B-8428): text 09 (78BE, row 8) "CONTINUE PLAY?" and TIME, then
       text 0B (7902, "PUSH 1P BUTTON", also the start screen) or 26 (7D88, "PUSH 2P BUTTON",
       only here) with CREDIT. New: text 09 = "PRESS FIRE TO CONTINUE" (row 8, column 5, colour
       01), text 26 = TIME (colour 03) and CREDIT, and 841B LDA #$0B -> #$26 */
    static const uint8_t old09[] = { 0x01, 0x21, 0x09, 'C', 'O', 'N', 'T', 'I', 'N', 'U', 'E', ' ', 'P', 'L', 'A',
        'Y', '?', 0x03, 0x03, 0x01, 0x21, 0x6c, 'T', 'I', 'M', 'E', 0x00 };
    static const uint8_t old26[] = { 0x02, 0x22, 0x49, 'P', 'U', 'S', 'H', ' ', '2', 'P', ' ', 'B', 'U', 'T', 'T',
        'O', 'N', 0x03, 0x01, 0x01, 0x23, 0x6b, 'C', 'R', 'E', 'D', 'I', 'T', 0x00 };
    static const uint8_t new26[] = { 0x03, 0x21, 0x6c, 'T', 'I', 'M', 'E', 0x03, 0x01, 0x01, 0x23, 0x6b,
        'C', 'R', 'E', 'D', 'I', 'T', 0x00 };
    uint8_t *p = x->main_bank_rom[1] + 0x3885;
    uint8_t *t09 = x->main_bank_rom[1] + 0x38be, *t26 = x->main_bank_rom[1] + 0x3d88;
    uint8_t *lda = x->main_rom + 0x041b;
    int n = 0;
    if (p[0] == 0x02 && p[1] == 0x22 && p[2] == 0x46 && !memcmp(p + 3, old_text, 20) && p[23] == 0x03) {
        memcpy(p + 3, new_text, 20);
        n++;
    }
    if (!memcmp(t09, old09, sizeof old09) && !memcmp(t26, old26, sizeof old26) && lda[0] == 0x86 && lda[1] == 0x0b) {
        t09[2] = 0x05;                                       /* 2109 -> 2105: centred */
        memcpy(t09 + 3, cont_text, 22);
        t09[25] = 0x00;
        memcpy(t26, new26, sizeof new26);
        lda[1] = 0x26;
        n++;
    }
    return n;
}

void xain_set_sound(Xain *x, int on)
{
    int i;
    x->sound = on;
    for (i = 0; i < 2; i++) {
        x->ym[i].out = on ? x->ymbuf[i] : 0;
        x->ym[i].out_mask = XAIN_YMBUF - 1;
    }
    x->audio_pos = x->ym[0].out_pos;
    x->audio_frac = 0;
}

int xain_take_audio(Xain *x, int16_t *dst, int max, int rate)
{
    /* input rate = XAIN_YM_CLOCK / 72; position = audio_pos + audio_frac / D with D = 72 * rate:
       each output sample advances the position by XAIN_YM_CLOCK / D */
    uint32_t d = 72u * x->ym[0].fm_div * (uint32_t)rate;
    uint32_t end = x->ym[0].out_pos;
    int n = 0;
    if (!x->sound) return 0;
    /* signed distances: below rate FM/2 one output sample can step past `end` (by one) */
    if ((int32_t)(end - x->audio_pos) > XAIN_YMBUF / 2) x->audio_pos = end - XAIN_YMBUF / 2;   /* overrun */
    while (n < max && (int32_t)(end - x->audio_pos) >= 2) {
        uint32_t i0 = x->audio_pos & (XAIN_YMBUF - 1), i1 = (x->audio_pos + 1) & (XAIN_YMBUF - 1);
        int32_t a = x->ymbuf[0][i0] + x->ymbuf[1][i0];
        int32_t b = x->ymbuf[0][i1] + x->ymbuf[1][i1];
        int32_t w = (int32_t)(((x->audio_frac >> 4) * 256u) / (d >> 4));   /* 0..255 */
        int32_t v = a + (((b - a) * w) >> 8);
        dst[n++] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        x->audio_frac += XAIN_YM_CLOCK;
        while (x->audio_frac >= d) {
            x->audio_frac -= d;
            x->audio_pos++;
        }
    }
    return n;
}

void xain_palette_rgb(const Xain *x, int i, uint8_t rgb[3])
{
    /* xBGR 444: palette = G:R, palette_ext = B */
    rgb[0] = (uint8_t)((x->palette[i] & 0x0f) * 0x11);
    rgb[1] = (uint8_t)((x->palette[i] >> 4) * 0x11);
    rgb[2] = (uint8_t)((x->palette_ext[i] & 0x0f) * 0x11);
}
