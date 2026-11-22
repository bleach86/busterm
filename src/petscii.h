#ifndef PETSCII_H
#define PETSCII_H

/* PETSCII: what a Commodore 64's screen does with each byte, for boards
 * that speak it. Its control codes move the cursor, set the colour, clear
 * the screen, reverse, delete and insert, and switch the character set;
 * its other codes are characters, drawn with the X16's own PETSCII font. */

#include <stdint.h>

void petscii_reset(void);
void petscii_feed(uint8_t c);

#endif
