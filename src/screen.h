#ifndef SCREEN_H
#define SCREEN_H

/* The screen: the X16's text mode, the first row busterm's status line and
 * the rows under it the terminal's, its rows 0 on.
 *
 *   ANSI      80 by 29, the PC's colours and character set (code page 437),
 *             the keyboard in ISO mode
 *   PETSCII   40 or 80 by 25, as a Commodore's screen: its colours, the
 *             X16's own PETSCII font, the keyboard typing PETSCII */

#include <stdint.h>

/* the terminal's size, and which it is */
extern uint8_t screen_cols, screen_rows;
extern uint8_t screen_petscii;

/* where the cursor is, and the colours being drawn in: background in the
 * high nibble, foreground in the low, the PC's or the C64's colour
 * numbers */
extern uint8_t screen_row, screen_col, screen_attr;

/* the scrolling region, rows top to bottom, inclusive */
extern uint8_t screen_top, screen_bottom;

/* the screen made ANSI's, or PETSCII's |cols| wide; cleared */
void screen_ansi(void);
void screen_petscii_mode(uint8_t cols);

/* PETSCII's two character sets: lower and upper case, or upper case and
 * graphics */
void screen_charset(uint8_t lower);

/* character |c| at the cursor (the PC's, or a Commodore screen code),
 * which moves on; past the last column ANSI's next character goes on the
 * next line, and a Commodore's cursor goes there at once */
void screen_put(uint8_t c);

void screen_goto(uint8_t row, uint8_t col);
void screen_cr(void);
void screen_lf(void); /* down a row, scrolling at the region's bottom */
void screen_reverse_lf(void);

/* |n| cells cleared from (row, col), in the current background */
void screen_clear_cells(uint8_t row, uint8_t col, uint8_t n);
void screen_clear_rows(uint8_t from, uint8_t to);

/* the region's rows top to bottom moved up (or down) |n|, the gap cleared */
void screen_scroll_up(uint8_t top, uint8_t bottom, uint8_t n);
void screen_scroll_down(uint8_t top, uint8_t bottom, uint8_t n);

/* a Commodore's DEL and INST: the row's rest moved left over the cursor,
 * or right to make room at it */
void screen_delete_char(void);
void screen_insert_char(void);

/* the cursor, drawn as the cell's colours swapped (PETSCII: the character
 * reversed) */
extern uint8_t screen_cursor_on;
void screen_cursor_show(void);
void screen_cursor_hide(void);

/* VRAM's address of a cell by the screen's own rows (0 the status line,
 * 1 to 29 the terminal's), for windows drawn over the terminal */
uint32_t screen_cell(uint8_t row, uint8_t col);

/* something else drew: the terminal finds its place again */
void screen_forget(void);

/* the status line: |text|, padded to the width */
void screen_status(const char *text);

/* back to the X16's own screen, for leaving */
void screen_restore(void);

#endif
