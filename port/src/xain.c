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

static uint8_t main_read(void *ctx, uint16_t a)
{
    Xain *x = ctx;
    if (a >= 0x3900 && a < 0x3980) return x->spriteram[a - 0x3800];
    switch (a) {
    case 0x3a00: return (uint8_t)~x->in_p1;
    case 0x3a01: return (uint8_t)~x->in_p2;
    case 0x3a02: return x->dsw0;
    case 0x3a03: return x->dsw1;
    case 0x3a04: return mcu_host_read(&x->mcu);
    case 0x3a05:
        /* bits 0-1, 6-7 unused (1), bit 2 coin 3 (active low), bits 3-4 MCU flags, bit 5 VBLANK */
        return (uint8_t)(0xc3 | (x->coin3 ? 0 : 0x04) | mcu_host_status(&x->mcu) | (x->vblank ? 0x20 : 0));
    case 0x3a06:
        mcu_host_reset(&x->mcu);
        return 0xff;
    default:
        return 0x00;                                    /* unmapped, palette (write only) */
    }
}

static void main_write(void *ctx, uint16_t a, uint8_t v)
{
    Xain *x = ctx;
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

static void run_slice(Xain *x, int32_t cycles)
{
    if (xain_prof) {
        xain_prof(-1);
        x->cpu_run(&x->main, cycles);
        xain_prof(0);
        x->cpu_run(&x->sub, cycles);
        xain_prof(1);
        x->cpu_run(&x->snd, cycles);
        xain_prof(2);
        x->snd.firq_line = (uint8_t)ym2203_advance(&x->ym[0], cycles * 2);
        ym2203_advance(&x->ym[1], cycles * 2);
        xain_prof(3);
        mcu_run(&x->mcu, cycles / 2);
        xain_prof(4);
        return;
    }
    x->cpu_run(&x->main, cycles);
    x->cpu_run(&x->sub, cycles);
    x->cpu_run(&x->snd, cycles);
    /* YM2203 at 3 MHz: 2 clocks per 6809 cycle; YM #1 IRQ -> sound FIRQ */
    x->snd.firq_line = (uint8_t)ym2203_advance(&x->ym[0], cycles * 2);
    ym2203_advance(&x->ym[1], cycles * 2);
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
    if (end - x->audio_pos > XAIN_YMBUF / 2) x->audio_pos = end - XAIN_YMBUF / 2;   /* overrun */
    while (n < max && end - x->audio_pos >= 2) {
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
