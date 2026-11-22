#ifndef VERA_H
#define VERA_H

/* VERA, the X16's video chip: its registers, and its address ports.
 *
 * busterm draws through data port 1. The ROM's interrupt (keyboard, mouse,
 * cursor) uses port 0 and may leave ADDRSEL at 0, so port 1's address is
 * only ever set with interrupts off, and ADDRSEL put back to 0. Writes to
 * DATA1 after that need no care: nothing else moves port 1. */

#include <stdint.h>

#define VERA_ADDR_L (*(volatile uint8_t *)0x9F20)
#define VERA_ADDR_M (*(volatile uint8_t *)0x9F21)
#define VERA_ADDR_H (*(volatile uint8_t *)0x9F22)
#define VERA_DATA0 (*(volatile uint8_t *)0x9F23)
#define VERA_DATA1 (*(volatile uint8_t *)0x9F24)
#define VERA_CTRL (*(volatile uint8_t *)0x9F25)
/* DC_VIDEO with DCSEL 0 (bit 4 shows layer 0), DC_VSTOP with DCSEL 1 (the
 * picture's last line, in 2s of the 480) */
#define VERA_DC_VIDEO (*(volatile uint8_t *)0x9F29)
#define VERA_DC_VSTOP (*(volatile uint8_t *)0x9F2C)
#define VERA_DCSEL1 0x02
#define VERA_LAYER0_ON 0x10
#define VERA_L0_CONFIG (*(volatile uint8_t *)0x9F2D)
#define VERA_L0_MAPBASE (*(volatile uint8_t *)0x9F2E)
#define VERA_L0_TILEBASE (*(volatile uint8_t *)0x9F2F)
#define VERA_L0_HSCROLL_L (*(volatile uint8_t *)0x9F30)
#define VERA_L0_HSCROLL_H (*(volatile uint8_t *)0x9F31)
#define VERA_L0_VSCROLL_L (*(volatile uint8_t *)0x9F32)
#define VERA_L0_VSCROLL_H (*(volatile uint8_t *)0x9F33)
#define VERA_L1_CONFIG (*(volatile uint8_t *)0x9F34)
#define VERA_L1_MAPBASE (*(volatile uint8_t *)0x9F35)
#define VERA_L1_TILEBASE (*(volatile uint8_t *)0x9F36)

/* ADDR_H: bit 0 the address's bit 16, bits 7 to 4 the increment */
#define VERA_INC1 0x10
#define VERA_INC2 0x20
#define VERA_DEC1 0x18 /* backwards a byte */

/* the 16 colours, in VRAM */
#define VERA_PALETTE 0x1FA00UL

static inline void irq_off(void) { __asm__ volatile("sei" ::: "memory"); }
static inline void irq_on(void) { __asm__ volatile("cli" ::: "memory"); }

/* port 1 at |addr|, moving on by |inc| (VERA_INC1, VERA_INC2) a byte */
static inline void vera_port1(uint32_t addr, uint8_t inc) {
    irq_off();
    VERA_CTRL = 1;
    VERA_ADDR_L = (uint8_t)addr;
    VERA_ADDR_M = (uint8_t)(addr >> 8);
    VERA_ADDR_H = (uint8_t)(addr >> 16) | inc;
    VERA_CTRL = 0;
    irq_on();
}

/* port 0 at |addr|; interrupts must be off while port 0 is in use */
static inline void vera_port0(uint32_t addr, uint8_t inc) {
    VERA_CTRL = 0;
    VERA_ADDR_L = (uint8_t)addr;
    VERA_ADDR_M = (uint8_t)(addr >> 8);
    VERA_ADDR_H = (uint8_t)(addr >> 16) | inc;
}

#endif
