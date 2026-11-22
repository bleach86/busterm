#ifndef SCREEN_H
#define SCREEN_H

/* The screen: the X16's 80 by 30 text mode, with the PC's 16 colours and
 * its character set (code page 437). The first row is busterm's status
 * line; the 29 under it are the terminal's, its rows 0 to 28. */

#include <stdint.h>

#define SCREEN_COLS 80
#define SCREEN_ROWS 29 /* the terminal's */

/* where the cursor is, and the colours being drawn in: background in the
 * high nibble, foreground in the low, the PC's colour numbers */
extern uint8_t screen_row, screen_col, screen_attr;

/* the scrolling region, rows top to bottom, inclusive */
extern uint8_t screen_top, screen_bottom;

void screen_init(void);

/* the PC's character |c| at the cursor, which moves on; past the last
 * column, the next character goes on the next line */
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

/* the cursor, drawn as the cell's colours swapped */
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
