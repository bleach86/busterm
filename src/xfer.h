#ifndef XFER_H
#define XFER_H

/* What transfer.c and zmodem.c share. zmodem.c's code is in banked RAM, in
 * BANK_CODE (loaded from BUSTERM.BNK as busterm starts), so main RAM has
 * room for the rest: IN_BANK marks it, FAR its ways in. It runs with BANK_CODE picked, and
 * picking another bank would take it away from under itself, so it reaches
 * the other banks only through the NEAR functions here, in main RAM, which
 * pick the bank and then the one before again. Neither FAR nor NEAR is
 * inlined: a way into the bank into main RAM's code, or a bank picker
 * into the bank's (the Makefile's bankcheck makes sure no bank is picked
 * in the bank). */

#include <stdint.h>

#define IN_BANK __attribute__((section(".bank")))
#define FAR __attribute__((section(".bank"), noinline))
#define NEAR __attribute__((noinline))

#define SECOND 60 /* jiffies */
#define CAN 0x18

/* what get() gives besides a byte */
#define TIMEOUT 0x100
#define STOPPED 0x200 /* Esc, or the call's gone */

/* the file, what's known of it, and how it's going */
extern char name[41];
extern unsigned long size, got;
extern uint8_t zmodem, sending;

/* transfer.c's, in main RAM: NEAR those that open or close a window,
 * which picks BANK_WINDOWS */
uint16_t get(uint16_t wait);
uint8_t uart_get_within(uint16_t wait);
NEAR void progress(uint8_t now);
void cancel(void);
void zcancel(void);
NEAR void tell(const char *s, uint8_t with_bytes);
void fail(const char *why);
uint8_t create(void);
char *put_text(char *p, const char *s);
char *put_number(char *p, unsigned long n);

/* ZMODEM's subpacket, in its own bank (BANK_ZMODEM), for the bank's code:
 * a byte put at |at| or got from it; |n| of it written to the file, or read
 * from it (as many as it read); ZFILE's name and size, as it gave them */
NEAR void zbank_put(uint16_t at, uint8_t c);
NEAR uint8_t zbank_get(uint16_t at);
NEAR void zbank_write(uint16_t n);
NEAR uint16_t zbank_read(uint16_t n);
NEAR void zbank_file(uint16_t n);
/* the file, |name|, opened to read at |pos| (read up to it in the bank) */
NEAR uint8_t open_at(unsigned long pos);

/* zmodem.c's, in BANK_CODE: called with it picked */
FAR void zreceive(void);
FAR void zsend(void);

#endif
