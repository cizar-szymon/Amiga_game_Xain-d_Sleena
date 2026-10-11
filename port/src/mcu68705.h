/* MC68705P5 with the Taito host interface (MAME 0.289 m68705.cpp + taito68705.cpp).
 * The MCU runs at 3 MHz / 4 = 750 000 cycles per second (half the 6809 cycle rate).
 */
#ifndef MCU68705_H
#define MCU68705_H

#include "m6805.h"

typedef struct Mcu68705 {
    M6805 cpu;
    uint8_t rom[0x800];         /* address space image: user EPROM 0080-0784, bootstrap 0785-07F7, vectors */
    uint8_t ram[0x80];          /* 0010-007F */
    uint8_t port_in[3], port_latch[3], port_ddr[3];
    /* timer/counter */
    uint8_t tdr, tcr, prescale, divisor, source, timer_line;
    /* Taito interface */
    uint8_t latch_driven, reset_input, host_flag, mcu_flag;
    uint8_t host_latch, mcu_latch, pa_output, pb_output;
    int32_t icount;             /* cycles left in the current time slice */
    int idle;                   /* skip the idle loop of pz-0.113 (exact, see mcu68705.c) */
    int idle_code_ok;           /* the ROM has that idle loop (checked once by mcu_init) */
} Mcu68705;

/* rom: 2 KB pz-0.113; boot: 115 B bootstrap (may be NULL) */
void mcu_init(Mcu68705 *m, const uint8_t *rom, const uint8_t *boot);
void mcu_reset(Mcu68705 *m);
/* runs whole instructions until `cycles` MCU cycles are used (the rest carries over) */
void mcu_run(Mcu68705 *m, int32_t cycles);

/* host (main CPU) side */
uint8_t mcu_host_read(Mcu68705 *m);          /* 3A04: data from the MCU, clears the MCU flag */
void mcu_host_write(Mcu68705 *m, uint8_t v); /* 3A0E: data to the MCU, sets the host flag + IRQ */
void mcu_host_reset(Mcu68705 *m);            /* 3A06 read: reset pulse */
/* 3A05 bits 3-4: bit 3 = MCU flag (active low), bit 4 = host flag (active low) */
uint8_t mcu_host_status(const Mcu68705 *m);

#endif
