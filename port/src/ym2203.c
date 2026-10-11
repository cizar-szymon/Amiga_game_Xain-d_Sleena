/* YM2203, see ym2203.h. C99 integer port of ymfm 1.x as in MAME 0.289
 * (ymfm_fm.ipp, ymfm_opn.cpp, ymfm_ssg.cpp; BSD-3-Clause, Copyright (c) 2021 Aaron Giles).
 */
#include "ym2203.h"
#include <string.h>

/* the Amiga build (vbcc) has the FM output in 68k assembler as well (ym2203_68k.s) */
#ifdef __VBCC__
#define YM2203_ASM
int32_t ym2203_fm_output_68k(YM2203 *ym, uint32_t t, int full);
#endif

#define OPERATORS 12
#define EG_ATTACK 0
#define EG_DECAY 1
#define EG_SUSTAIN 2
#define EG_RELEASE 3
#define EG_QUIET 0x380
#define KEYON_NORMAL 0
#define KEYON_CSM 2
#define MOD_CH2 0x04            /* YM2203.modified: bits 0-2 channels to prepare, */
#define MOD_WRITE 0x08          /* bit 3 any FM register write */

#define BIT(v, n) (((v) >> (n)) & 1u)
#define BITS(v, s, n) (((v) >> (s)) & ((1u << (n)) - 1u))

/* ---- tables (ymfm_fm.ipp) ---- */

static const uint16_t sin_table[256] = {
    0x859,0x6c3,0x607,0x58b,0x52e,0x4e4,0x4a6,0x471,0x443,0x41a,0x3f5,0x3d3,0x3b5,0x398,0x37e,0x365,
    0x34e,0x339,0x324,0x311,0x2ff,0x2ed,0x2dc,0x2cd,0x2bd,0x2af,0x2a0,0x293,0x286,0x279,0x26d,0x261,
    0x256,0x24b,0x240,0x236,0x22c,0x222,0x218,0x20f,0x206,0x1fd,0x1f5,0x1ec,0x1e4,0x1dc,0x1d4,0x1cd,
    0x1c5,0x1be,0x1b7,0x1b0,0x1a9,0x1a2,0x19b,0x195,0x18f,0x188,0x182,0x17c,0x177,0x171,0x16b,0x166,
    0x160,0x15b,0x155,0x150,0x14b,0x146,0x141,0x13c,0x137,0x133,0x12e,0x129,0x125,0x121,0x11c,0x118,
    0x114,0x10f,0x10b,0x107,0x103,0x0ff,0x0fb,0x0f8,0x0f4,0x0f0,0x0ec,0x0e9,0x0e5,0x0e2,0x0de,0x0db,
    0x0d7,0x0d4,0x0d1,0x0cd,0x0ca,0x0c7,0x0c4,0x0c1,0x0be,0x0bb,0x0b8,0x0b5,0x0b2,0x0af,0x0ac,0x0a9,
    0x0a7,0x0a4,0x0a1,0x09f,0x09c,0x099,0x097,0x094,0x092,0x08f,0x08d,0x08a,0x088,0x086,0x083,0x081,
    0x07f,0x07d,0x07a,0x078,0x076,0x074,0x072,0x070,0x06e,0x06c,0x06a,0x068,0x066,0x064,0x062,0x060,
    0x05e,0x05c,0x05b,0x059,0x057,0x055,0x053,0x052,0x050,0x04e,0x04d,0x04b,0x04a,0x048,0x046,0x045,
    0x043,0x042,0x040,0x03f,0x03e,0x03c,0x03b,0x039,0x038,0x037,0x035,0x034,0x033,0x031,0x030,0x02f,
    0x02e,0x02d,0x02b,0x02a,0x029,0x028,0x027,0x026,0x025,0x024,0x023,0x022,0x021,0x020,0x01f,0x01e,
    0x01d,0x01c,0x01b,0x01a,0x019,0x018,0x017,0x017,0x016,0x015,0x014,0x014,0x013,0x012,0x011,0x011,
    0x010,0x00f,0x00f,0x00e,0x00d,0x00d,0x00c,0x00c,0x00b,0x00a,0x00a,0x009,0x009,0x008,0x008,0x007,
    0x007,0x007,0x006,0x006,0x005,0x005,0x005,0x004,0x004,0x004,0x003,0x003,0x003,0x002,0x002,0x002,
    0x002,0x001,0x001,0x001,0x001,0x001,0x001,0x001,0x000,0x000,0x000,0x000,0x000,0x000,0x000,0x000
};

#define X(a) (((a) | 0x400) << 2)
/* exported for the 68k FM output (ym2203_68k.s) */
const uint16_t ym2203_power_table[256] = {
    X(0x3fa),X(0x3f5),X(0x3ef),X(0x3ea),X(0x3e4),X(0x3df),X(0x3da),X(0x3d4),
    X(0x3cf),X(0x3c9),X(0x3c4),X(0x3bf),X(0x3b9),X(0x3b4),X(0x3ae),X(0x3a9),
    X(0x3a4),X(0x39f),X(0x399),X(0x394),X(0x38f),X(0x38a),X(0x384),X(0x37f),
    X(0x37a),X(0x375),X(0x370),X(0x36a),X(0x365),X(0x360),X(0x35b),X(0x356),
    X(0x351),X(0x34c),X(0x347),X(0x342),X(0x33d),X(0x338),X(0x333),X(0x32e),
    X(0x329),X(0x324),X(0x31f),X(0x31a),X(0x315),X(0x310),X(0x30b),X(0x306),
    X(0x302),X(0x2fd),X(0x2f8),X(0x2f3),X(0x2ee),X(0x2e9),X(0x2e5),X(0x2e0),
    X(0x2db),X(0x2d6),X(0x2d2),X(0x2cd),X(0x2c8),X(0x2c4),X(0x2bf),X(0x2ba),
    X(0x2b5),X(0x2b1),X(0x2ac),X(0x2a8),X(0x2a3),X(0x29e),X(0x29a),X(0x295),
    X(0x291),X(0x28c),X(0x288),X(0x283),X(0x27f),X(0x27a),X(0x276),X(0x271),
    X(0x26d),X(0x268),X(0x264),X(0x25f),X(0x25b),X(0x257),X(0x252),X(0x24e),
    X(0x249),X(0x245),X(0x241),X(0x23c),X(0x238),X(0x234),X(0x230),X(0x22b),
    X(0x227),X(0x223),X(0x21e),X(0x21a),X(0x216),X(0x212),X(0x20e),X(0x209),
    X(0x205),X(0x201),X(0x1fd),X(0x1f9),X(0x1f5),X(0x1f0),X(0x1ec),X(0x1e8),
    X(0x1e4),X(0x1e0),X(0x1dc),X(0x1d8),X(0x1d4),X(0x1d0),X(0x1cc),X(0x1c8),
    X(0x1c4),X(0x1c0),X(0x1bc),X(0x1b8),X(0x1b4),X(0x1b0),X(0x1ac),X(0x1a8),
    X(0x1a4),X(0x1a0),X(0x19c),X(0x199),X(0x195),X(0x191),X(0x18d),X(0x189),
    X(0x185),X(0x181),X(0x17e),X(0x17a),X(0x176),X(0x172),X(0x16f),X(0x16b),
    X(0x167),X(0x163),X(0x160),X(0x15c),X(0x158),X(0x154),X(0x151),X(0x14d),
    X(0x149),X(0x146),X(0x142),X(0x13e),X(0x13b),X(0x137),X(0x134),X(0x130),
    X(0x12c),X(0x129),X(0x125),X(0x122),X(0x11e),X(0x11b),X(0x117),X(0x114),
    X(0x110),X(0x10c),X(0x109),X(0x106),X(0x102),X(0x0ff),X(0x0fb),X(0x0f8),
    X(0x0f4),X(0x0f1),X(0x0ed),X(0x0ea),X(0x0e7),X(0x0e3),X(0x0e0),X(0x0dc),
    X(0x0d9),X(0x0d6),X(0x0d2),X(0x0cf),X(0x0cc),X(0x0c8),X(0x0c5),X(0x0c2),
    X(0x0be),X(0x0bb),X(0x0b8),X(0x0b5),X(0x0b1),X(0x0ae),X(0x0ab),X(0x0a8),
    X(0x0a4),X(0x0a1),X(0x09e),X(0x09b),X(0x098),X(0x094),X(0x091),X(0x08e),
    X(0x08b),X(0x088),X(0x085),X(0x082),X(0x07e),X(0x07b),X(0x078),X(0x075),
    X(0x072),X(0x06f),X(0x06c),X(0x069),X(0x066),X(0x063),X(0x060),X(0x05d),
    X(0x05a),X(0x057),X(0x054),X(0x051),X(0x04e),X(0x04b),X(0x048),X(0x045),
    X(0x042),X(0x03f),X(0x03c),X(0x039),X(0x036),X(0x033),X(0x030),X(0x02d),
    X(0x02a),X(0x028),X(0x025),X(0x022),X(0x01f),X(0x01c),X(0x019),X(0x016),
    X(0x014),X(0x011),X(0x00e),X(0x00b),X(0x008),X(0x006),X(0x003),X(0x000)
};
#undef X

static const uint32_t increment_table[64] = {
    0x00000000, 0x00000000, 0x10101010, 0x10101010, 0x10101010, 0x10101010, 0x11101110, 0x11101110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
    0x11111111, 0x21112111, 0x21212121, 0x22212221, 0x22222222, 0x42224222, 0x42424242, 0x44424442,
    0x44444444, 0x84448444, 0x84848484, 0x88848884, 0x88888888, 0x88888888, 0x88888888, 0x88888888
};

static const uint8_t detune_table[32][4] = {
    { 0, 0, 1, 2 }, { 0, 0, 1, 2 }, { 0, 0, 1, 2 }, { 0, 0, 1, 2 },
    { 0, 1, 2, 2 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 }, { 0, 1, 2, 3 },
    { 0, 1, 2, 4 }, { 0, 1, 3, 4 }, { 0, 1, 3, 4 }, { 0, 1, 3, 5 },
    { 0, 2, 4, 5 }, { 0, 2, 4, 6 }, { 0, 2, 4, 6 }, { 0, 2, 5, 7 },
    { 0, 2, 5, 8 }, { 0, 3, 6, 8 }, { 0, 3, 6, 9 }, { 0, 3, 7, 10 },
    { 0, 4, 8, 11 }, { 0, 4, 8, 12 }, { 0, 4, 9, 13 }, { 0, 5, 10, 14 },
    { 0, 5, 11, 16 }, { 0, 6, 12, 17 }, { 0, 6, 13, 19 }, { 0, 7, 14, 20 },
    { 0, 8, 16, 22 }, { 0, 8, 16, 22 }, { 0, 8, 16, 22 }, { 0, 8, 16, 22 }
};

static const int16_t ssg_amplitudes[32] = {
        0,   32,   78,  141,  178,  222,  262,  306,
      369,  441,  509,  585,  701,  836,  965, 1112,
     1334, 1595, 1853, 2146, 2576, 3081, 3576, 4135,
     5000, 6006, 7023, 8155, 9963,11976,14132,16382
};

/* waveform[index] = abs_sin_attenuation(index) | sign << 15 (filled by ym2203_init) */
uint16_t ym2203_wave[1024];

static void make_wave(void)
{
    uint32_t index;
    for (index = 0; index < 1024; index++) {
        uint32_t i = index;
        if (BIT(i, 8)) i = ~i;
        ym2203_wave[index] = (uint16_t)(sin_table[i & 0xff] | (BIT(index, 9) << 15));
    }
}

/* ---- registers ---- */

static uint32_t ch_block_freq(const YM2203 *ym, uint32_t ch)
{
    return ((uint32_t)(ym->regs[0xa4 + ch] & 0x3f) << 8) | ym->regs[0xa0 + ch];
}

static uint32_t multi_freq_mode(const YM2203 *ym) { return BITS(ym->regs[0x27], 6, 2) != 0; }
static uint32_t csm_mode(const YM2203 *ym) { return BITS(ym->regs[0x27], 6, 2) == 2; }

static uint32_t effective_rate(uint32_t raw, uint32_t ksr)
{
    return raw == 0 ? 0 : (raw + ksr > 63 ? 63 : raw + ksr);
}

/* opn_registers::cache_operator_data + compute_phase_step (no LFO on OPN) */
static void cache_operator(YM2203 *ym, uint32_t ch, YMOperator *o)
{
    uint32_t opoffs = o->opoffs;
    uint32_t block_freq = ch_block_freq(ym, ch);
    uint32_t keycode, ksrval, fnum, block, step, multiple, sl;
    int32_t detune;
    uint8_t r30 = ym->regs[0x30 + opoffs], r50 = ym->regs[0x50 + opoffs];
    uint8_t r60 = ym->regs[0x60 + opoffs], r70 = ym->regs[0x70 + opoffs], r80 = ym->regs[0x80 + opoffs];

    if (multi_freq_mode(ym) && ch == 2) {
        int num = opoffs == 2 ? 1 : opoffs == 10 ? 2 : opoffs == 6 ? 0 : -1;
        if (num >= 0)
            block_freq = ((uint32_t)(ym->regs[0xac + num] & 0x3f) << 8) | ym->regs[0xa8 + num];
    }
    keycode = BITS(block_freq, 10, 4) << 1;
    keycode |= BIT(0xfe80u, BITS(block_freq, 7, 4));
    detune = detune_table[keycode][BITS(r30, 4, 3) & 3];
    if (BIT(r30, 6)) detune = -detune;
    multiple = BITS(r30, 0, 4) * 2;
    if (multiple == 0) multiple = 1;

    fnum = BITS(block_freq, 0, 11) << 1;
    block = BITS(block_freq, 11, 3);
    step = (fnum << block) >> 2;
    step = (uint32_t)((int32_t)step + detune) & 0x1ffff;
    o->phase_step = (step * multiple) >> 1;

    o->total_level = (uint32_t)(ym->regs[0x40 + opoffs] & 0x7f) << 3;
    sl = BITS(r80, 4, 4);
    sl |= (sl + 1) & 0x10;
    o->eg_sustain = sl << 5;
    ksrval = keycode >> (BITS(r50, 6, 2) ^ 3);
    o->eg_rate[EG_ATTACK] = (uint8_t)effective_rate(BITS(r50, 0, 5) * 2, ksrval);
    o->eg_rate[EG_DECAY] = (uint8_t)effective_rate(BITS(r60, 0, 5) * 2, ksrval);
    o->eg_rate[EG_SUSTAIN] = (uint8_t)effective_rate(BITS(r70, 0, 5) * 2, ksrval);
    o->eg_rate[EG_RELEASE] = (uint8_t)effective_rate(BITS(r80, 0, 4) * 4 + 2, ksrval);
    o->ssg_en = (uint8_t)BIT(ym->regs[0x90 + opoffs], 3);
    o->ssg_mode = (uint8_t)BITS(ym->regs[0x90 + opoffs], 0, 3);
}

/* every register write marks its channel modified, so prepare (cache_operator) runs before
   the next clock: the cached values equal the registers whenever they are used */
#define ssg_eg_enable(ym, o) ((void)(ym), (uint32_t)(o)->ssg_en)
#define ssg_eg_mode(ym, o) ((void)(ym), (uint32_t)(o)->ssg_mode)

/* ---- operator (fm_operator) ---- */

static void start_attack(YM2203 *ym, YMOperator *o, int is_restart)
{
    if (o->env_state == EG_ATTACK) return;
    o->env_state = EG_ATTACK;
    if (!is_restart)
        o->ssg_inverted = (uint8_t)(ssg_eg_enable(ym, o) & BIT(ssg_eg_mode(ym, o), 2));
    if (!is_restart)
        o->phase = 0;
    if (o->eg_rate[EG_ATTACK] >= 62)
        o->env_att = 0;
}

static void start_release(YMOperator *o)
{
    if (o->env_state >= EG_RELEASE) return;
    o->env_state = EG_RELEASE;
    if (o->ssg_inverted) {
        o->env_att = (uint16_t)((0x200 - o->env_att) & 0x3ff);
        o->ssg_inverted = 0;
    }
}

static int op_prepare(YM2203 *ym, uint32_t ch, YMOperator *o)
{
    uint32_t keystate = o->keyon_live != 0;
    cache_operator(ym, ch, o);
    if (keystate != o->key_state) {
        o->key_state = (uint8_t)keystate;
        if (keystate) start_attack(ym, o, 0);
        else start_release(o);
    }
    o->keyon_live &= (uint8_t)~(1 << KEYON_CSM);
    return o->env_state != EG_RELEASE || o->env_att < EG_QUIET;
}

static void clock_ssg_eg(YM2203 *ym, YMOperator *o)
{
    uint32_t mode;
    if (!BIT(o->env_att, 9)) return;
    mode = ssg_eg_mode(ym, o);
    if (BIT(mode, 0)) {
        o->ssg_inverted = (uint8_t)(BIT(mode, 2) ^ BIT(mode, 1));
        if (o->env_state != EG_ATTACK)
            o->env_att = o->ssg_inverted ? 0x200 : 0x3ff;
    } else {
        o->ssg_inverted ^= (uint8_t)BIT(mode, 1);
        if (o->env_state == EG_DECAY || o->env_state == EG_SUSTAIN)
            start_attack(ym, o, 1);
        if (BIT(mode, 1) == 0)
            o->phase = 0;
    }
    if (o->env_state == EG_RELEASE)
        o->env_att = 0x3ff;
}

static void clock_envelope(YM2203 *ym, YMOperator *o, uint32_t env_counter)
{
    uint32_t rate, rate_shift, bits, inc;
    if (o->env_state == EG_ATTACK && o->env_att == 0)
        o->env_state = EG_DECAY;
    if (o->env_state == EG_DECAY && o->env_att >= o->eg_sustain)
        o->env_state = EG_SUSTAIN;
    rate = o->eg_rate[o->env_state];
    rate_shift = rate >> 2;
    env_counter <<= rate_shift;
    if (BITS(env_counter, 0, 11) != 0) return;
    bits = BITS(env_counter, rate_shift <= 11 ? 11 : rate_shift, 3);
    inc = BITS(increment_table[rate], 4 * bits, 4);
    if (o->env_state == EG_ATTACK) {
        if (rate < 62)
            o->env_att = (uint16_t)(o->env_att + ((~(uint32_t)o->env_att * inc) >> 4));
    } else {
        uint32_t att = o->env_att;
        if (!ssg_eg_enable(ym, o)) att += inc;
        else if (att < 0x200) att += 4 * inc;
        if (att >= 0x400) att = 0x3ff;
        o->env_att = (uint16_t)att;
    }
}

/* the envelope part of the operator output (fm_operator::compute_volume), kept in eg4 by every
   visit that can change env_att, ssg_inverted or total_level: the attenuation << 2, 0x1000 when
   silent (env_att > EG_QUIET: the output is 0) */
static void op_set_eg(YMOperator *o)
{
    uint32_t env = o->env_att;
    if (env > EG_QUIET) {
        o->eg4 = 0x1000;
        return;
    }
    if (o->ssg_inverted) env = (0x200 - env) & 0x3ff;
    env += o->total_level;
    if (env > 0x3ff) env = 0x3ff;
    o->eg4 = env << 2;
}

/* fm_operator::compute_volume: an input >= 0x1000 gives 0 (attenuation_to_volume) */
static int32_t op_volume(const YMOperator *o, uint32_t phase)
{
    uint32_t sinatt = ym2203_wave[phase & 0x3ff];
    uint32_t x = (sinatt & 0x7fff) + o->eg4;
    int32_t r;
    if (x >= 0x1000) return 0;
    r = (int32_t)(ym2203_power_table[x & 0xff] >> (x >> 8));
    return (sinatt & 0x8000) ? -r : r;
}

/*
 * Lazy phases. Between envelope ticks (every third FM sample) a clocked operator without SSG-EG
 * only adds phase_step to its phase, and whether it is clocked at all ("live": not released to
 * silence, ymfm's operators are clocked anyway but such a phase is never heard, see clock_fm)
 * only changes at a tick or a prepare. So the operators are visited at ticks, at prepares
 * (their channel) and, with SSG-EG, every sample; in between the phase at sample t is
 * phase + phase_step * (t - pt). Identical to clocking every sample (mod 2^32).
 */
static void op_sync(YMOperator *o, uint32_t t)
{
    if (o->live) o->phase += o->phase_step * (t - o->pt);
    o->pt = t;
}

static void op_set_live(YMOperator *o)
{
    o->live = (uint8_t)!(o->env_state == EG_RELEASE && o->env_att == 0x3ff && !o->ssg_en);
}

static uint32_t op_phase(const YMOperator *o, uint32_t t)
{
    return (o->live ? o->phase + o->phase_step * (t - o->pt) : o->phase) >> 10;
}

/* ---- channel ---- */

/* fm_channel::output_4op. The operator outputs are at most 8168 in magnitude, so the sums of
   two (the modulation inputs 5-7) fit in 16 bits and the sum of up to four never reaches the
   clamp at +-32767: the plain sums below equal ymfm's clamped ones. */
static int32_t channel_output(YM2203 *ym, uint32_t ch, uint32_t t)
{
    YMChannel *c = &ym->ch[ch];
    uint8_t rb0 = ym->regs[0xb0 + ch];
    uint32_t fb = BITS(rb0, 3, 3);
    int32_t opmod = 0, o1, o2, o3, o4;
    YMOperator *const *op = c->op;
    if (fb) opmod = (c->feedback[0] + c->feedback[1]) >> (10 - fb);
    o1 = c->feedback_in = (int16_t)op_volume(op[0], op_phase(op[0], t) + (uint32_t)opmod);
    switch (rb0 & 7) {
    case 0:                                             /* 1 -> 2 -> 3 -> 4 */
        o2 = op_volume(op[1], op_phase(op[1], t) + (uint32_t)(o1 >> 1));
        o3 = op_volume(op[2], op_phase(op[2], t) + (uint32_t)(o2 >> 1));
        return op_volume(op[3], op_phase(op[3], t) + (uint32_t)(o3 >> 1));
    case 1:                                             /* (1 + 2) -> 3 -> 4 */
        o2 = op_volume(op[1], op_phase(op[1], t));
        o3 = op_volume(op[2], op_phase(op[2], t) + (uint32_t)((o1 + o2) >> 1));
        return op_volume(op[3], op_phase(op[3], t) + (uint32_t)(o3 >> 1));
    case 2:                                             /* (1 + (2 -> 3)) -> 4 */
        o2 = op_volume(op[1], op_phase(op[1], t));
        o3 = op_volume(op[2], op_phase(op[2], t) + (uint32_t)(o2 >> 1));
        return op_volume(op[3], op_phase(op[3], t) + (uint32_t)((o1 + o3) >> 1));
    case 3:                                             /* ((1 -> 2) + 3) -> 4 */
        o2 = op_volume(op[1], op_phase(op[1], t) + (uint32_t)(o1 >> 1));
        o3 = op_volume(op[2], op_phase(op[2], t));
        return op_volume(op[3], op_phase(op[3], t) + (uint32_t)((o2 + o3) >> 1));
    case 4:                                             /* (1 -> 2) + (3 -> 4) */
        o2 = op_volume(op[1], op_phase(op[1], t) + (uint32_t)(o1 >> 1));
        o3 = op_volume(op[2], op_phase(op[2], t));
        o4 = op_volume(op[3], op_phase(op[3], t) + (uint32_t)(o3 >> 1));
        return o4 + o2;
    case 5:                                             /* 1 -> (2, 3, 4) */
        o2 = op_volume(op[1], op_phase(op[1], t) + (uint32_t)(o1 >> 1));
        o3 = op_volume(op[2], op_phase(op[2], t) + (uint32_t)(o1 >> 1));
        o4 = op_volume(op[3], op_phase(op[3], t) + (uint32_t)(o1 >> 1));
        return o4 + o2 + o3;
    case 6:                                             /* (1 -> 2) + 3 + 4 */
        o2 = op_volume(op[1], op_phase(op[1], t) + (uint32_t)(o1 >> 1));
        o3 = op_volume(op[2], op_phase(op[2], t));
        o4 = op_volume(op[3], op_phase(op[3], t));
        return o4 + o2 + o3;
    default:                                            /* 1 + 2 + 3 + 4 */
        o2 = op_volume(op[1], op_phase(op[1], t));
        o3 = op_volume(op[2], op_phase(op[2], t));
        o4 = op_volume(op[3], op_phase(op[3], t));
        return o4 + o1 + o2 + o3;
    }
}

/* ymfm roundtrip_fp: the 10.3 floating point DAC. With m = |v| (v ^ v >> 31) the low
   max(0, 6 - n) bits are cleared, n = number of leading zeros of m in 15 bits: m >= 0x4000 -> 6
   bits, >= 0x2000 -> 5, ... >= 0x200 -> 1, below none */
static int32_t roundtrip_fp(int32_t v)
{
    int32_t m;
    if (v < -32768) return -32768;
    if (v > 32767) return 32767;
    m = v ^ (v >> 31);
    if (m < 0x200) return v;
    if (m >= 0x4000) return v & ~63;
    if (m >= 0x2000) return v & ~31;
    if (m >= 0x1000) return v & ~15;
    if (m >= 0x800) return v & ~7;
    if (m >= 0x400) return v & ~3;
    return v & ~1;
}

/* fm_engine_base::clock + output for one FM sample */
static void clock_fm(YM2203 *ym)
{
    uint32_t ch, t;
    int k;
    int32_t sum = 0;
    t = ++ym->total_samples;
    if (ym->modified || ym->prepare_count++ >= 4096) {
        /* ymfm prepares all channels after any write; a channel whose registers and key-on
           bits did not change keeps its cache and key state, so only the written channels are
           prepared (identical result). The active mask is computed for all of them. */
        uint32_t mask = ym->modified ? (uint32_t)(ym->modified & 7) : 7;
        ym->active = 0;
        for (ch = 0; ch < 3; ch++) {
            int act = 0;
            for (k = 0; k < 4; k++) {
                YMOperator *o = ym->ch[ch].op[k];
                if (mask & (1u << ch)) {
                    op_sync(o, t - 1);                  /* the phase before this sample */
                    act |= op_prepare(ym, ch, o);
                    op_set_live(o);
                    if (o->live && !o->ssg_en)
                        o->ssg_inverted = 0;            /* as clocking this sample does */
                    op_set_eg(o);
                } else {
                    act |= o->env_state != EG_RELEASE || o->env_att < EG_QUIET;
                }
            }
            if (act) ym->active |= (uint8_t)(1 << ch);
        }
        ym->ssg_ops = 0;
        for (k = 0; k < OPERATORS; k++)
            if (ym->op[k].ssg_en) ym->ssg_ops |= (uint16_t)(1 << k);
        ym->modified = 0;
        ym->prepare_count = 0;
    }
    if (BITS(++ym->env_counter, 0, 2) == 3)
        ym->env_counter += 1;
    for (ch = 0; ch < 3; ch++) {
        YMChannel *c = &ym->ch[ch];
        c->feedback[0] = c->feedback[1];
        c->feedback[1] = c->feedback_in;
    }
    {
        /* fm_operator::clock for all operators (each only changes itself, so the order does
           not matter); the envelope runs every third sample. An operator silent for good
           (released to the end, no SSG-EG) is skipped: only its phase would change, and the
           phase is reset by the next key on before it can be heard again. Without a tick only
           the operators with SSG-EG need a visit (op_sync). */
        YMOperator *o = ym->op, *end = ym->op + OPERATORS;
        uint32_t ec = ym->env_counter;
        if (BITS(ec, 0, 2) == 0) {
            ec >>= 2;
            for (; o < end; o++) {
                op_sync(o, t - 1);
                o->pt = t;
                if (!o->live)
                    continue;
                if (o->ssg_en) clock_ssg_eg(ym, o);
                else o->ssg_inverted = 0;
                clock_envelope(ym, o, ec);
                o->phase += o->phase_step;
                op_set_live(o);
                op_set_eg(o);
            }
        } else if (ym->ssg_ops) {
            for (k = 0; o < end; o++, k++)
                if (ym->ssg_ops & (1 << k)) {
                    op_sync(o, t - 1);
                    o->pt = t;
                    clock_ssg_eg(ym, o);
                    o->phase += o->phase_step;
                    op_set_live(o);
                    op_set_eg(o);
                }
        }
    }
#ifdef YM2203_ASM
    if (!(ym2203_debug & 4)) {                          /* the same in 68k assembler */
        if (ym->div_count) ym2203_fm_output_68k(ym, t, 0);
        else ym->last_fm = roundtrip_fp(ym2203_fm_output_68k(ym, t, 1));
        return;
    }
#endif
    if (ym->div_count) {
        /* fm_div > 1: no output in this sample; operator 1 still runs for the feedback */
        for (ch = 0; ch < 3; ch++)
            if (ym->active & (1 << ch)) {
                YMChannel *c = &ym->ch[ch];
                uint32_t fb = BITS(ym->regs[0xb0 + ch], 3, 3);
                if (fb) {
                    int32_t opmod = (c->feedback[0] + c->feedback[1]) >> (10 - fb);
                    c->feedback_in = (int16_t)op_volume(c->op[0], op_phase(c->op[0], t) + (uint32_t)opmod);
                }
            }
        return;
    }
    for (ch = 0; ch < 3; ch++)
        if (ym->active & (1 << ch))
            sum += channel_output(ym, ch, t);
    ym->last_fm = roundtrip_fp(sum);
}

/* ---- SSG (ssg_engine) ---- */

/* the envelope amplitude for env_state (ssg_engine::output; also clamps env_state at 32 when
   holding, which ymfm does on every output) */
static void ssg_envelope(YM2203 *ym)
{
    uint8_t r13 = ym->regs[0x0d];
    uint32_t cont = BIT(r13, 3), att = BIT(r13, 2), alt = BIT(r13, 1), hold = BIT(r13, 0);
    uint32_t envvol;
    if ((hold | (cont ^ 1)) && ym->env_state >= 32) {
        ym->env_state = 32;
        envvol = ((att ^ alt) & cont) ? 31 : 0;
    } else {
        uint32_t a = att;
        if (alt) a ^= BIT(ym->env_state, 5);
        envvol = (ym->env_state & 31) ^ (a ? 0 : 31);
    }
    ym->ssg_envamp = ssg_amplitudes[envvol];
}

/* register derived values; recomputed before the next step after an SSG register write */
static void ssg_cache(YM2203 *ym)
{
    int ch;
    for (ch = 0; ch < 3; ch++) {
        uint32_t vol = (uint32_t)(ym->regs[8 + ch] & 0x0f) * 2;
        if (vol) vol |= 1;
        ym->ssg_amp[ch] = ssg_amplitudes[vol];
        ym->ssg_period[ch] = ym->regs[2 * ch] | ((uint32_t)(ym->regs[2 * ch + 1] & 0x0f) << 8);
    }
    ym->ssg_noise_period = ym->regs[6] & 0x1f;
    ym->ssg_env_period = (uint32_t)(ym->regs[0x0b] | (ym->regs[0x0c] << 8));
    ssg_envelope(ym);
    ym->ssg_dirty = 0;
}

/* A+B+C for the current state (ssg_engine::output without the envelope part) */
static int32_t ssg_out(const YM2203 *ym)
{
    uint8_t mix = ym->regs[7];
    int32_t sum = 0;
    int ch;
    for (ch = 0; ch < 3; ch++) {
        uint32_t noise_on = BIT(mix, 3 + ch) | ym->noise_state;
        uint32_t tone_on = BIT(mix, ch) | ym->tone_state[ch];
        if (noise_on & tone_on & 1)
            sum += BIT(ym->regs[8 + ch], 4) ? ym->ssg_envamp : ym->ssg_amp[ch];
    }
    return sum;
}

/*
 * The counters of ssg_engine::clock. A counter with period p has its event (toggle, shift,
 * envelope step) at ++count >= p: the first one after k0 = max(1, p - count) steps, then every
 * max(p, 1) steps (the noise every max(2, 2 * period) steps). Skipping s steps at once gives
 * exactly the state of stepping them one by one.
 */
static int32_t first_event(uint32_t period, uint32_t count)
{
    int32_t k = (int32_t)period - (int32_t)count;
    return k < 1 ? 1 : k;
}

/* advances a counter by s steps; returns the number of events */
static uint32_t skip_counter(uint32_t *count, uint32_t period, uint32_t every, int32_t s)
{
    int32_t k0 = first_event(period, *count);
    uint32_t n;
    if (s < k0) {
        *count += (uint32_t)s;
        return 0;
    }
    if ((uint32_t)(s - k0) < every) {                  /* usual: one event (no division) */
        *count = (uint32_t)(s - k0);
        return 1;
    }
    if (every == 1) {                                   /* period 0 or 1: every step */
        *count = 0;
        return (uint32_t)(s - k0) + 1;
    }
    n = 1 + (uint32_t)(s - k0) / every;
    *count = (uint32_t)(s - k0) % every;
    return n;
}

static void noise_shift(YM2203 *ym, uint32_t n)
{
    while (n--) {
        ym->noise_state ^= (BIT(ym->noise_state, 0) ^ BIT(ym->noise_state, 3)) << 17;
        ym->noise_state >>= 1;
    }
}

static uint32_t noise_threshold(const YM2203 *ym)
{
    /* (count >> 1) >= np && count != 1: count >= max(2, 2 * np) */
    return ym->ssg_noise_period > 1 ? 2 * ym->ssg_noise_period : 2;
}

/* the envelope holds (ssg_envelope: a fixed amplitude until register 0D is written) */
static int ssg_env_held(const YM2203 *ym)
{
    uint8_t r13 = ym->regs[0x0d];
    return (BIT(r13, 0) | (BIT(r13, 3) ^ 1)) && ym->env_state >= 32;
}

/* advances the counters that do not influence the output by `total` steps at once: the tone
   counters of the channels in tone_mask, the noise, the envelope */
static void ssg_skip_counters(YM2203 *ym, int tone_mask, int noise, int env, int32_t total)
{
    int ch;
    for (ch = 0; ch < 3; ch++)
        if (tone_mask & (1 << ch))
            ym->tone_state[ch] ^= skip_counter(&ym->tone_count[ch], ym->ssg_period[ch],
                                               ym->ssg_period[ch] ? ym->ssg_period[ch] : 1, total) & 1;
    if (noise) {
        uint32_t nthr = noise_threshold(ym);
        noise_shift(ym, skip_counter(&ym->noise_count, nthr, nthr, total));
    }
    if (env) {
        uint32_t n = skip_counter(&ym->env_count, ym->ssg_env_period,
                                  ym->ssg_env_period ? ym->ssg_env_period : 1, total);
        if (n) {
            ym->env_state += n;                         /* ssg_envelope clamps at 32 when holding */
            ssg_envelope(ym);
        }
    }
}

/* the steps of a silent stretch (ssg_run) on all counters; before an SSG register write */
static void ssg_flush(YM2203 *ym)
{
    if (ym->ssg_skip) {
        ssg_skip_counters(ym, 7, 1, 1, ym->ssg_skip);
        ym->ssg_skip = 0;
    }
}

/*
 * `steps` SSG steps at once, exactly as stepping them one by one (ssg_engine::clock + output;
 * verified: identical sound). Between two events that can change the output (a tone toggle of
 * a channel that is heard, a noise shift when a heard channel uses noise, an envelope step when
 * a heard channel uses an envelope that does not hold) the output does not change, so it is
 * added for the whole stretch; the other counters do not influence the output and are advanced
 * at the end. A channel is not heard with volume 0, or in envelope mode with the envelope held
 * at 0. When no counter is relevant at all (the usual silent SSG), the output stays the same
 * until a register write: the steps are only counted (ssg_skip) and done at once before the
 * next SSG register write (ssg_flush) or every 4096 steps (the noise shifts one by one).
 * Returns the sum of the outputs of the steps.
 */
extern unsigned long ym2203_ssg_iter, ym2203_ssg_runs;
/* which counters can change the output (see ssg_run); depends on the registers and on whether
   the envelope holds, so it is computed again after a register write or an envelope step */
static void ssg_relevance(YM2203 *ym)
{
    uint8_t mix = ym->regs[7], held = (uint8_t)ssg_env_held(ym);
    int ch;
    ym->ssg_tone_rel = ym->ssg_noise_rel = ym->ssg_env_rel = 0;
    for (ch = 0; ch < 3; ch++) {
        uint8_t envm = (uint8_t)BIT(ym->regs[8 + ch], 4);
        uint8_t heard = (uint8_t)(envm ? !held || ym->ssg_envamp != 0 : ym->ssg_amp[ch] != 0);
        if (heard && !BIT(mix, ch)) ym->ssg_tone_rel |= (uint8_t)(1 << ch);
        ym->ssg_noise_rel |= (uint8_t)(heard && !BIT(mix, 3 + ch));
        ym->ssg_env_rel |= (uint8_t)(heard && envm && !held);
    }
    ym->ssg_rel_valid = 1;
}

static int32_t ssg_run(YM2203 *ym, int32_t steps)
{
    int32_t acc = 0, cur, total = steps;
    uint8_t tone_rel[3], noise_rel, env_rel;
    uint32_t nthr;
    int ch;
    if (ym->ssg_dirty) {
        ssg_cache(ym);
        ym->ssg_rel_valid = 0;
    }
    if (!ym->ssg_rel_valid)
        ssg_relevance(ym);
    tone_rel[0] = (uint8_t)(ym->ssg_tone_rel & 1);
    tone_rel[1] = (uint8_t)((ym->ssg_tone_rel >> 1) & 1);
    tone_rel[2] = (uint8_t)((ym->ssg_tone_rel >> 2) & 1);
    noise_rel = ym->ssg_noise_rel;
    env_rel = ym->ssg_env_rel;
    cur = ssg_out(ym);
    ym2203_ssg_runs++;
    /* no relevant counter: stays so until a register write (see ssg_steps) */
    ym->ssg_quiet = (uint8_t)!(ym->ssg_tone_rel | noise_rel | env_rel);
    ym->ssg_qcur = cur;
    if (ym->ssg_quiet) {
        ym->ssg_skip += steps;
        if (ym->ssg_skip >= 4096)
            ssg_flush(ym);
        return steps * cur;
    }
    ssg_flush(ym);
    nthr = noise_threshold(ym);
    while (steps > 0) {
        int32_t m = steps, k;
        ym2203_ssg_iter++;
        for (ch = 0; ch < 3; ch++)
            if (tone_rel[ch] && (k = first_event(ym->ssg_period[ch], ym->tone_count[ch])) < m)
                m = k;
        if (noise_rel && (k = first_event(nthr, ym->noise_count)) < m)
            m = k;
        if (env_rel && (k = first_event(ym->ssg_env_period, ym->env_count)) < m)
            m = k;
        /* steps 1..m-1: no change; step m: the counters that reach their event act */
        acc += (m - 1) * cur;
        for (ch = 0; ch < 3; ch++)
            if (tone_rel[ch] && skip_counter(&ym->tone_count[ch], ym->ssg_period[ch],
                                             ym->ssg_period[ch] ? ym->ssg_period[ch] : 1, m))
                ym->tone_state[ch] ^= 1;
        if (noise_rel)
            noise_shift(ym, skip_counter(&ym->noise_count, nthr, nthr, m));
        if (env_rel && skip_counter(&ym->env_count, ym->ssg_env_period,
                                    ym->ssg_env_period ? ym->ssg_env_period : 1, m)) {
            ym->env_state++;
            ssg_envelope(ym);
            ym->ssg_rel_valid = 0;                      /* it may hold now (from the next call) */
        }
        cur = ssg_out(ym);
        acc += cur;
        steps -= m;
    }
    ssg_skip_counters(ym, (!tone_rel[0]) | (!tone_rel[1]) << 1 | (!tone_rel[2]) << 2, !noise_rel, !env_rel,
                      total);
    return acc;
}

/* ---- timers ---- */

static void check_irq(YM2203 *ym)
{
    ym->irq = (uint8_t)((ym->status & 0x03) != 0);
}

static uint32_t timer_period(const YM2203 *ym, int t)
{
    if (t == 0)
        return 1024u - (uint32_t)(ym->regs[0x24] << 2 | (ym->regs[0x25] & 3));
    return 16u * (256u - ym->regs[0x26]);
}

static void update_timer(YM2203 *ym, int t, int enable, int32_t delta)
{
    if (enable && !ym->timer_running[t]) {
        int32_t period = (int32_t)timer_period(ym, t) + delta;
        ym->timer_left[t] = period * OPERATORS * ym->prescale;   /* input clocks from now */
        ym->timer_running[t] = 1;
    } else if (!enable) {
        ym->timer_running[t] = 0;
    }
}

static void timer_expired(YM2203 *ym, int t)
{
    if (ym->regs[0x27] & (t == 0 ? 0x04 : 0x08))
        ym->status |= (uint8_t)(1 << t);
    if (t == 0 && csm_mode(ym)) {
        int k;
        for (k = 0; k < 4; k++)
            ym->ch[2].op[k]->keyon_live |= 1 << KEYON_CSM;
        ym->modified |= MOD_CH2;
    }
}

/* diagnostics (time measurements): bit 0 = no SSG steps, bit 1 = no FM clock, bit 2 = the FM
   output in C (not 68k) */
int ym2203_debug;
unsigned long ym2203_ssg_iter, ym2203_ssg_runs;
void (*ym2203_trace)(const YM2203 *ym, uint32_t word);

/* ---- public ---- */

#define OFS(t, f) ((unsigned)((const char *)&((t *)0)->f - (const char *)0))

int ym2203_layout_ok(void)
{
#ifdef YM2203_ASM
    /* the offsets used by ym2203_68k.s */
    return OFS(YMOperator, phase) == 0 && OFS(YMOperator, pt) == 4 && OFS(YMOperator, phase_step) == 8 &&
           OFS(YMOperator, eg4) == 20 && OFS(YMOperator, live) == 37 && OFS(YMChannel, op) == 0 &&
           OFS(YMChannel, feedback) == 16 && OFS(YMChannel, feedback_in) == 20 && sizeof(YMChannel) == 24 &&
           OFS(YM2203, regs) == 0 && OFS(YM2203, active) == 263 && OFS(YM2203, ch) == 768;
#else
    return 1;
#endif
}

void ym2203_init(YM2203 *ym)
{
    memset(ym, 0, sizeof(*ym));
    if (!ym2203_wave[0]) make_wave();
    ym->fm_div = 1;
    ym2203_reset(ym);
}

void ym2203_reset(YM2203 *ym)
{
    /* operator order per channel (opn operator_map): 0,6,3,9 / 1,7,4,10 / 2,8,5,11;
       register offset of operator n: n + n / 3 */
    static const uint8_t map[3][4] = { { 0, 6, 3, 9 }, { 1, 7, 4, 10 }, { 2, 8, 5, 11 } };
    int i, k;
    if (ym2203_trace) ym2203_trace(ym, YM_TRACE_RESET);
    memset(ym->regs, 0, sizeof ym->regs);
    ym->addr = 0;
    ym->status = 0;
    ym->irq = 0;
    ym->prescale = 6;
    ym->timer_running[0] = ym->timer_running[1] = 0;
    for (i = 0; i < OPERATORS; i++) {
        YMOperator *o = &ym->op[i];
        memset(o, 0, sizeof(*o));
        o->opoffs = (uint8_t)(i + i / 3);
        o->env_att = 0x3ff;
        o->env_state = EG_RELEASE;
        op_set_eg(o);
    }
    for (i = 0; i < 3; i++) {
        ym->ch[i].feedback[0] = ym->ch[i].feedback[1] = ym->ch[i].feedback_in = 0;
        for (k = 0; k < 4; k++)
            ym->ch[i].op[k] = &ym->op[map[i][k]];
    }
    ym->env_counter = 0;
    ym->modified = 7 | MOD_WRITE;
    ym->active = 7;
    ym->prepare_count = 0;
    ym->last_fm = 0;
    for (i = 0; i < 3; i++) ym->tone_count[i] = ym->tone_state[i] = 0;
    ym->env_count = ym->env_state = 0;
    ym->noise_count = 0;
    ym->noise_state = 1;
    ym->ssg_dirty = 1;
    ym->ssg_acc = ym->ssg_n = 0;
    ym->ssg_skip = 0;
    ym->ssg_quiet = ym->ssg_varied = 0;
    ym->ssg_rel_valid = 0;
    ym->ssg_qcur = 0;
    ym->fm_phase = ym->ssg_phase = 0;
    ym->div_count = 0;
}

void ym2203_write(YM2203 *ym, int offset, uint8_t data)
{
    uint8_t a;
    if (ym2203_trace) ym2203_trace(ym, (uint32_t)(offset & 1) << 8 | data);
    if (!(offset & 1)) {
        ym->addr = data;
        if (data == 0x2d) ym->prescale = 6;
        else if (data == 0x2e && ym->prescale == 6) ym->prescale = 3;
        else if (data == 0x2f) ym->prescale = 2;
        return;
    }
    a = ym->addr;
    if (a < 0x10) {                                     /* SSG */
        ssg_flush(ym);                                  /* the counted steps with the old values */
        ym->regs[a] = data;
        if (a == 0x0d) ym->env_state = 0;
        ym->ssg_dirty = 1;
        return;
    }
    if (a == 0x27) {                                    /* mode: timers */
        uint8_t reset = 0;
        /* the mode bits (6-7) only matter for channel 3 */
        ym->modified |= (uint8_t)(MOD_WRITE | (((ym->regs[a] ^ data) & 0xc0) ? MOD_CH2 : 0));
        ym->regs[a] = data;
        if (data & 0x20) reset |= 0x02;
        if (data & 0x10) reset |= 0x01;
        ym->status &= (uint8_t)~reset;
        update_timer(ym, 1, data & 0x02, -(int32_t)(ym->total_samples & 15));
        update_timer(ym, 0, data & 0x01, 0);
        check_irq(ym);
        return;
    }
    if (a >= 0x30)                                      /* operator / channel registers */
        ym->modified |= (uint8_t)(MOD_WRITE | ((a & 0xf8) == 0xa8 ? MOD_CH2 : (a & 3) == 3 ? 0 : 1 << (a & 3)));
    else if (a == 0x28)                                 /* key on/off */
        ym->modified |= (uint8_t)(MOD_WRITE | ((data & 3) == 3 ? 0 : 1 << (data & 3)));
    else
        ym->modified |= MOD_WRITE;
    if ((a & 0xf0) == 0xa0) {                           /* latched frequency pairs */
        uint8_t latch;
        if ((a & 3) == 3) return;
        latch = (uint8_t)(0xb8 | ((a >> 3) & 1));
        if (a & 4) ym->regs[latch] = data & 0x3f;
        else {
            ym->regs[a] = data;
            ym->regs[a | 4] = ym->regs[latch];
        }
        return;
    }
    if ((a & 0xf8) == 0xb8) return;
    ym->regs[a] = data;
    if (a == 0x28) {                                    /* key on/off */
        uint32_t ch = data & 3;
        int k;
        if (ch == 3) return;
        for (k = 0; k < 4; k++) {
            YMOperator *o = ym->ch[ch].op[k];
            o->keyon_live = (uint8_t)((o->keyon_live & ~1u) | BIT(data, 4 + k));
        }
    }
}

/* `steps` SSG steps (ssg_run); a quiet SSG (no counter relevant, output fixed) inline */
static void ssg_steps(YM2203 *ym, int32_t steps)
{
    if (ym->ssg_quiet && !ym->ssg_dirty) {
        ym->ssg_skip += steps;
        if (ym->ssg_skip >= 4096)
            ssg_flush(ym);
        ym->ssg_acc += steps * ym->ssg_qcur;
    } else {
        ym->ssg_acc += ssg_run(ym, steps);
        ym->ssg_varied = 1;
    }
    ym->ssg_n += steps;
}

int ym2203_advance(YM2203 *ym, int32_t clocks)
{
    int32_t unit = OPERATORS * ym->prescale;            /* input clocks per FM sample */
    int ssg_shift = ym->prescale == 6 ? 4 : ym->prescale == 3 ? 3 : 2;   /* 16, 8 or 4 clocks per SSG step */
    int t, no_ssg = ym2203_debug & 1, no_fm = ym2203_debug & 2;
    if (ym2203_trace) ym2203_trace(ym, YM_TRACE_ADVANCE | (uint32_t)clocks);
    for (t = 0; t < 2; t++) {
        if (!ym->timer_running[t]) continue;
        ym->timer_left[t] -= clocks;
        while (ym->timer_running[t] && ym->timer_left[t] <= 0) {
            int32_t over = -ym->timer_left[t];
            timer_expired(ym, t);
            ym->timer_left[t] = (int32_t)timer_period(ym, t) * unit - over;
        }
    }
    check_irq(ym);
    if (!ym->out) {                                     /* timers only */
        ym->fm_phase += clocks;
        while (ym->fm_phase >= unit) {
            ym->fm_phase -= unit;
            ym->total_samples++;
        }
        return ym->irq;
    }
    while (clocks > 0) {
        /* up to the next FM sample; the SSG steps on the way are done at once by ssg_run
           (before the FM sample, as one by one: an SSG step at the same clock comes first) */
        int32_t n = unit - ym->fm_phase, steps;
        if (n > clocks) n = clocks;
        clocks -= n;
        ym->ssg_phase += n;
        ym->fm_phase += n;
        if (ym->fm_phase < unit)
            break;
        steps = ym->ssg_phase >> ssg_shift;
        if (steps) {
            ym->ssg_phase -= steps << ssg_shift;
            if (!no_ssg) ssg_steps(ym, steps);
        }
        ym->fm_phase = 0;
        if (!no_fm)
            clock_fm(ym);
        if (ym->div_count == 0) {
            int32_t fm = ym->last_fm * 2, ssg;
            /* the mean SSG output over the sample (a quiet SSG: its fixed output) */
            if (!ym->ssg_n) ssg = 0;
            else if (!ym->ssg_varied) ssg = ym->ssg_qcur;
            else ssg = (int32_t)((uint32_t)ym->ssg_acc / (uint32_t)ym->ssg_n);   /* acc >= 0 */
            ym->ssg_acc = ym->ssg_n = 0;
            ym->ssg_varied = 0;
            /* MAME xain: SSG A/B/C x 0.50, FM x 0.40; fm / 5 as a multiplication (exact
               for |fm| <= 65536, rounded towards 0 as the division) */
            fm = fm < 0 ? -(int32_t)(((uint32_t)-fm * 52429u) >> 18) : (int32_t)(((uint32_t)fm * 52429u) >> 18);
            ym->out[ym->out_pos & ym->out_mask] = ssg / 2 + fm;
            ym->out_pos++;
        }
        if (++ym->div_count >= ym->fm_div)
            ym->div_count = 0;
    }
    {
        /* the SSG steps done so far, so that register writes after this call act on the
           later steps only (as one by one) */
        int32_t steps = ym->ssg_phase >> ssg_shift;
        if (steps) {
            ym->ssg_phase -= steps << ssg_shift;
            if (!no_ssg) ssg_steps(ym, steps);
        }
    }
    return ym->irq;
}
