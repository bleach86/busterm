#ifndef ANSI_H
#define ANSI_H

/* ANSI-BBS: the escape codes BBSes send (ANSI.SYS's, with VT100's), drawn
 * on the screen, and the board's questions (where the cursor is, how big
 * the screen is) answered to the modem. */

#include <stdint.h>

void ansi_reset(void);
void ansi_feed(uint8_t c);

#endif
