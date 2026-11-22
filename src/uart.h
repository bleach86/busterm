#ifndef UART_H
#define UART_H

/* The modem's UART: channel A of the TL16C2550 on TexElec's Serial & ESP32
 * Network Card, at $9FE0, wired to the card's ESP32 (Zimodem).
 *
 * Its crystal is 14.7456 MHz, so a divisor of 8 is 115,200 baud. Auto flow
 * control is on: the UART itself drops RTS as its FIFO fills, and the modem
 * waits, so nothing is lost however long busterm takes over a byte. */

#include <stdint.h>

#define UART_BASE 0x9FE0

#define UART_RBR (*(volatile uint8_t *)(UART_BASE + 0)) /* read */
#define UART_THR (*(volatile uint8_t *)(UART_BASE + 0)) /* write */
#define UART_DLL (*(volatile uint8_t *)(UART_BASE + 0)) /* with DLAB */
#define UART_DLM (*(volatile uint8_t *)(UART_BASE + 1)) /* with DLAB */
#define UART_IER (*(volatile uint8_t *)(UART_BASE + 1))
#define UART_FCR (*(volatile uint8_t *)(UART_BASE + 2)) /* write */
#define UART_LCR (*(volatile uint8_t *)(UART_BASE + 3))
#define UART_MCR (*(volatile uint8_t *)(UART_BASE + 4))
#define UART_LSR (*(volatile uint8_t *)(UART_BASE + 5))
#define UART_MSR (*(volatile uint8_t *)(UART_BASE + 6))

#define LSR_READY 0x01 /* a byte received */
#define LSR_EMPTY 0x20 /* room to send */
#define MSR_DCD 0x80   /* the modem's carrier */

/* the divisor for 115,200 baud, 14.7456 MHz over 16 over it */
#define UART_DIVISOR_115200 8

void uart_init(uint16_t divisor);

/* a byte waiting */
static inline uint8_t uart_ready(void) { return UART_LSR & LSR_READY; }
static inline uint8_t uart_get(void) { return UART_RBR; }

/* the modem's carrier: online */
static inline uint8_t uart_carrier(void) { return UART_MSR & MSR_DCD; }

void uart_put(uint8_t c);
void uart_puts(const char *s);

#endif
