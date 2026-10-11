/* MC68705P5 + Taito host interface, see mcu68705.h. */
#include "mcu68705.h"
#include <string.h>

enum { TCR_PS = 0x07, TCR_PSC = 0x08, TCR_TIE = 0x10, TCR_TIN = 0x20, TCR_TIM = 0x40, TCR_TIR = 0x80 };
enum { SRC_CLOCK = 0, SRC_CLOCK_TIMER = 1, SRC_DISABLED = 2, SRC_TIMER = 3 };

static const uint8_t port_mask[3] = { 0x00, 0x00, 0xf0 };   /* port C is four bits wide */

static void set_timer_line(Mcu68705 *m, int state)
{
    uint8_t s = (uint8_t)(state != 0);
    if (s != m->timer_line) {
        m->timer_line = s;
        if (s) m->cpu.pending |= M6805_INT_TIMER;
        else m->cpu.pending &= (uint8_t)~M6805_INT_TIMER;     /* level-sensitive */
    }
}

static void set_irq_line(Mcu68705 *m, int state)
{
    m6805_set_irq(&m->cpu, state);
}

static uint8_t pa_value(const Mcu68705 *m)
{
    return (uint8_t)(m->pa_output & (m->latch_driven ? m->host_latch : 0xff));
}

static void pa_input(Mcu68705 *m, uint8_t v)
{
    m->port_in[0] = (uint8_t)(v & ~port_mask[0]);
}

/* taito68705_mcu_device_base::latch_control with PB1 (host) and PB2 (MCU) */
static void latch_control(Mcu68705 *m, uint8_t data)
{
    uint8_t old_pa = pa_value(m);
    uint8_t value = m->pb_output;

    if (data & 0x02) {
        m->latch_driven = 0;
        pa_input(m, 0xff);
        if (!(value & 0x02)) {
            m->host_flag = 0;
            set_irq_line(m, 0);
        }
    } else {
        m->latch_driven = 1;
        pa_input(m, m->host_latch);
    }
    if (!(data & 0x04)) {
        if (!m->reset_input) m->mcu_flag = 1;
        if (value & 0x04) m->mcu_latch = old_pa;
    }
    m->pb_output = data;
}

/* port output callback (port_cb_w): port A open drain -> pa_output, port B -> latch control */
static void port_cb(Mcu68705 *m, int n)
{
    if (n == 0)
        m->pa_output = (uint8_t)(m->port_latch[0] | ~m->port_ddr[0]);
    else if (n == 1)
        latch_control(m, m->port_latch[1]);
}

static void port_latch_w(Mcu68705 *m, int n, uint8_t v)
{
    uint8_t diff = (uint8_t)(m->port_latch[n] ^ v);
    m->port_latch[n] = v;
    if (diff & m->port_ddr[n]) port_cb(m, n);
}

static void port_ddr_w(Mcu68705 *m, int n, uint8_t v)
{
    if (v != m->port_ddr[n]) {
        m->port_ddr[n] = v;
        port_cb(m, n);
    }
}

static uint8_t port_r(Mcu68705 *m, int n)
{
    if (n == 2) {
        /* PC0 host flag (active high), PC1 MCU flag (active low) */
        uint8_t in = (uint8_t)((m->host_flag ? 0x01 : 0x00) | (m->mcu_flag ? 0x00 : 0x02) | 0xfc);
        m->port_in[2] = (uint8_t)(in & ~port_mask[2]);
    }
    return (uint8_t)(port_mask[n] | (m->port_latch[n] & m->port_ddr[n]) | (m->port_in[n] & ~m->port_ddr[n]));
}

static void tcr_w(Mcu68705 *m, uint8_t data)
{
    /* programmable timer (MOR bit TOPT clear in pz-0.113) */
    m->divisor = data & TCR_PS;
    m->source = (uint8_t)((data & (TCR_TIN | TCR_TIE)) >> 4);
    if (data & TCR_PSC) m->prescale = 0;
    m->tcr = (uint8_t)((m->tcr & (data & TCR_TIR)) | (data & ~(TCR_TIR | TCR_PSC)));
    set_timer_line(m, (m->tcr & TCR_TIR) && !(m->tcr & TCR_TIM));
}

static void timer_update(Mcu68705 *m, unsigned count)
{
    unsigned prescale, dec, interrupt;
    if (m->source == SRC_DISABLED) return;                  /* CLOCK_TIMER: external input stays high */
    prescale = (m->prescale & ((1u << m->divisor) - 1)) + (m->source == SRC_TIMER ? 0 : count);
    dec = prescale >> m->divisor;
    interrupt = (m->tdr ? (unsigned)m->tdr : 256u) <= dec;
    m->prescale = (uint8_t)(prescale & 0x7f);
    m->tdr = (uint8_t)(m->tdr - dec);
    if (interrupt) {
        m->tcr |= TCR_TIR;
        if (!(m->tcr & TCR_TIM)) set_timer_line(m, 1);
    }
}

static uint8_t mem_r(void *ctx, uint16_t a)
{
    Mcu68705 *m = ctx;
    if (a >= 0x80) return m->rom[a];
    if (a >= 0x10) return m->ram[a];
    switch (a) {
    case 0x00: case 0x01: case 0x02: return port_r(m, a);
    case 0x08: return m->tdr;
    case 0x09: return (uint8_t)(m->tcr & ~TCR_PSC);
    default: return 0x00;                                   /* unmapped (PCR 0B: EPROM control) */
    }
}

static void mem_w(void *ctx, uint16_t a, uint8_t v)
{
    Mcu68705 *m = ctx;
    if (a >= 0x80) return;
    if (a >= 0x10) { m->ram[a] = v; return; }
    switch (a) {
    case 0x00: case 0x01: case 0x02: port_latch_w(m, a, v); break;
    case 0x04: case 0x05: case 0x06: port_ddr_w(m, a - 4, v); break;
    case 0x08: m->tdr = v; break;
    case 0x09: tcr_w(m, v); break;
    default: break;
    }
}

/* the idle loop at 096 (see idle_skip) */
static const uint8_t idle_code[12] = { 0x3d, 0x3c, 0x27, 0x04, 0xad, 0x23, 0x20, 0xf8, 0x3d, 0x28, 0x27, 0xf4 };

void mcu_init(Mcu68705 *m, const uint8_t *rom, const uint8_t *boot)
{
    memset(m, 0, sizeof(*m));
    memcpy(m->rom, rom, 0x800);
    if (boot) memcpy(m->rom + 0x785, boot, 0x73);
    m->idle_code_ok = !memcmp(m->rom + 0x96, idle_code, sizeof idle_code);
    m6805_init(&m->cpu);
    m->cpu.read = mem_r;
    m->cpu.write = mem_w;
    m->cpu.rom = m->rom;
    m->cpu.ctx = m;
    memset(m->port_in, 0xff, sizeof m->port_in);
    memset(m->port_latch, 0xff, sizeof m->port_latch);
    m->divisor = 7;
    m->source = SRC_CLOCK;
    m->host_latch = m->mcu_latch = m->pa_output = m->pb_output = 0xff;
    mcu_reset(m);
}

/* MCU reset (m68705 device_reset): CPU, ports, timer */
static void cpu_reset(Mcu68705 *m)
{
    m6805_reset(&m->cpu);
    port_ddr_w(m, 0, 0x00);
    port_ddr_w(m, 1, 0x00);
    port_ddr_w(m, 2, 0x00);
    m->prescale = 0x7f;
    m->tdr = 0xff;
    m->tcr = 0x7f;
    m->cpu.pending = 0;
    m->timer_line = 0;
}

void mcu_reset(Mcu68705 *m)
{
    m->host_flag = 0;
    m->mcu_flag = 0;
    set_irq_line(m, 0);
    cpu_reset(m);
}

/*
 * Idle loop of pz-0.113 at 096: TST $3C / BEQ 09E / TST $28 / BEQ 096 (6+4+6+4 cycles) while
 * both are 0. Nothing outside the MCU changes during its time slice, so the rest of the slice
 * is done at once from any of the four instructions, including the instructions of the last,
 * partial turn: registers stay the same (TST leaves Z set, N clear), only PC, the cycles and the
 * timer change. When the timer could request an interrupt, only whole turns before it.
 */

/* x / 20 for 0 <= x <= 40000 (exact; no division in the per slice path) */
static int32_t div20(int32_t x)
{
    return (int32_t)(((uint32_t)x * 26215u) >> 19);
}

static void idle_skip(Mcu68705 *m)
{
    /* the four instructions of the loop and their cycles */
    static const uint16_t loop_pc[4] = { 0x096, 0x098, 0x09e, 0x0a0 };
    static const uint8_t loop_cost[4] = { 6, 4, 6, 4 };
    M6805 *c = &m->cpu;
    int32_t k, r, s, t;
    int phase, n, i;
    if (c->pc == 0x096) phase = 0;
    else if (c->pc == 0x098) phase = 1;
    else if (c->pc == 0x09e) phase = 2;
    else if (c->pc == 0x0a0) phase = 3;
    else return;
    if (m->ram[0x3c] || m->ram[0x28] || (c->cc & (M6805_CC_N | M6805_CC_Z)) != M6805_CC_Z)
        return;
    if (c->pending && !(c->cc & M6805_CC_I))
        return;
    if (!m->idle_code_ok)
        return;
    /* the instructions run while icount > 0: k whole turns (20 cycles), then n instructions of
       the next turn (s cycles), so that icount ends in (-6, 0] as one by one */
    k = div20(m->icount - 1);
    r = m->icount - k * 20;
    for (n = 0, s = 0; s < r; n++)
        s += loop_cost[(phase + n) & 3];
    t = k * 20 + s;
    if (!(c->cc & M6805_CC_I) && !(m->tcr & TCR_TIM) && m->source != SRC_DISABLED && m->source != SRC_TIMER) {
        /* the timer could interrupt: only up to the cycle before its request, whole turns */
        int32_t mask = (1 << m->divisor) - 1;
        int32_t room = ((int32_t)(m->tdr ? m->tdr : 256) << m->divisor) - (m->prescale & mask) - 1;
        if (t > room) {
            if (div20(room) < k) k = div20(room);
            n = 0;
            t = k * 20;
        }
    }
    if (t <= 0)
        return;
    m->icount -= t;
    c->total += (uint32_t)t;
    c->pc = loop_pc[(phase + n) & 3];
    if (m->divisor == 7) {
        timer_update(m, (unsigned)t);                   /* same as per instruction: mask 0x7f */
    } else {
        while (k--)
            for (i = 0; i < 4; i++) timer_update(m, loop_cost[(phase + i) & 3]);
        for (i = 0; i < n; i++)
            timer_update(m, loop_cost[(phase + i) & 3]);
    }
}

void mcu_run(Mcu68705 *m, int32_t cycles)
{
    m->icount += cycles;
    while (m->icount > 0) {
        int c;
        if (m->idle)
            idle_skip(m);
        if (m->icount <= 0)
            break;
        c = m6805_step(&m->cpu);
        m->icount -= c;
        timer_update(m, (unsigned)c);
    }
}

uint8_t mcu_host_read(Mcu68705 *m)
{
    m->mcu_flag = 0;
    return m->mcu_latch;
}

void mcu_host_write(Mcu68705 *m, uint8_t v)
{
    if (!m->reset_input) m->host_flag = 1;
    m->host_latch = v;
    if (m->latch_driven) pa_input(m, v);
    set_irq_line(m, m->host_flag);
}

void mcu_host_reset(Mcu68705 *m)
{
    /* reset_w(ASSERT) then reset_w(CLEAR); MAME applies the CPU reset line later (scheduler),
       when the interface's reset input is already released again */
    m->reset_input = 1;
    m->host_flag = 0;
    m->mcu_flag = 0;
    set_irq_line(m, 0);
    m->reset_input = 0;
    cpu_reset(m);
}

uint8_t mcu_host_status(const Mcu68705 *m)
{
    return (uint8_t)((m->mcu_flag ? 0 : 0x08) | (m->host_flag ? 0 : 0x10));
}
