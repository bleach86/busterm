#include "uart.h"

#define LCR_8N1 0x03
#define LCR_DLAB 0x80
#define FCR_ON_CLEAR 0x07    /* FIFOs on, both emptied */
#define FCR_ON_TRIGGER8 0x81 /* FIFOs on, RTS dropped at 8 bytes */
#define MCR_DTR 0x01
#define MCR_RTS 0x02
#define MCR_AFE 0x20 /* auto flow control, RTS and CTS */

void uart_init(uint16_t divisor) {
    UART_IER = 0; /* no interrupts: busterm polls */
    UART_LCR = LCR_DLAB;
    UART_DLL = (uint8_t)divisor;
    UART_DLM = (uint8_t)(divisor >> 8);
    UART_LCR = LCR_8N1;
    UART_FCR = FCR_ON_CLEAR;
    UART_FCR = FCR_ON_TRIGGER8;
    UART_MCR = MCR_DTR | MCR_RTS | MCR_AFE;
}

void uart_put(uint8_t c) {
    while (!(UART_LSR & LSR_EMPTY)) {
    }
    UART_THR = c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_put((uint8_t)*s++);
    }
}
