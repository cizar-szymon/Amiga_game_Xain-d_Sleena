/* Motorola 6805 (HMOS, 11-bit address: 68705P5) emulator, C99.
 * Follows MAME 0.289 m6805 (6805ops.hxx, s_hmos_cycles): cycles per instruction from the
 * table, 11 cycles for an interrupt, memory accesses in the same order as MAME.
 */
#ifndef M6805_H
#define M6805_H

#include <stdint.h>

enum {
    M6805_CC_C = 0x01, M6805_CC_Z = 0x02, M6805_CC_N = 0x04, M6805_CC_I = 0x08, M6805_CC_H = 0x10
};

/* pending interrupt bits */
enum { M6805_INT_IRQ = 0x01, M6805_INT_TIMER = 0x02 };

typedef struct M6805 M6805;

struct M6805 {
    uint8_t a, x, cc, s;        /* S: 0x60-0x7F */
    uint16_t pc, ppc;
    uint8_t pending;            /* M6805_INT_*: latched requests */
    uint8_t irq_pin;            /* /INT line asserted (for BIL/BIH) */
    uint32_t total;             /* executed cycles */

    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t data);
    const uint8_t *rom;         /* 2 KB address space image for opcode fetches (0x80-0x7FF) */
    void *ctx;
    void (*hook)(M6805 *cpu);   /* per-instruction hook for tests */
};

void m6805_init(M6805 *cpu);
void m6805_reset(M6805 *cpu);
/* one instruction (or interrupt entry); returns its cycles */
int m6805_step(M6805 *cpu);
/* /INT line: an assertion latches a pending request (cleared when taken) */
void m6805_set_irq(M6805 *cpu, int state);

#endif
