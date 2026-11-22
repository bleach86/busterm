#ifndef CAPTURE_H
#define CAPTURE_H

/* Capture: what the board sends, as it sends it (its ANSI or PETSCII codes
 * and all, so an ANSI screen captured is an .ANS file), added to the end
 * of a file in the SD card's FILES folder. It's gathered in banked RAM
 * and written 8K at a time, the file opened and closed each time, so the
 * screen keeps up and no file's left open. */

#include <stdint.h>

/* on */
extern uint8_t capturing;

/* started, into |name| (made if it isn't there, added to if it is): 1, or
 * 0 if it can't be */
uint8_t capture_start(const char *name);

/* a byte from the board, kept */
void capture_byte(uint8_t c);

/* what's gathered so far written, so the file on the card is all of it
 * (for sending it, say) */
void capture_flush(void);

/* stopped, what's gathered written: the bytes captured, all told; and
 * whether all of them got to the file */
unsigned long capture_stop(uint8_t *ok);

/* the file's name */
const char *capture_name(void);

#endif
