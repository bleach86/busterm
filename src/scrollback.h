#ifndef SCROLLBACK_H
#define SCROLLBACK_H

/* Scrollback: each row that scrolls off the terminal's top, kept in banked
 * RAM as VRAM had it (characters and colours), about 2,800 of them. A
 * change of screen (ANSI, PETSCII 40 or 80) starts it afresh, as its rows
 * only mean anything in their own font and colours. */

#include <stdint.h>

/* none kept */
void scrollback_clear(void);

/* the row at VRAM |addr|, |cells| wide, kept as the newest */
void scrollback_push(uint32_t addr, uint8_t cells);

/* the rows kept, looked through over the terminal until Esc: Up and Down
 * a row, Page Up and Page Down a screen, Home the oldest; Esc or Alt-B
 * closes it, as does Page Down at the bottom. |first| is the
 * key that opened it, acted on first. */
void scrollback_view(uint8_t first);

#endif
