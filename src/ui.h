#ifndef UI_H
#define UI_H

/* Windows over the terminal: a box with a title and a shadow, the screen
 * under it kept and put back as it closes. Rows are the screen's own: 0 is
 * the status line. Windows open and close in order, one over another. */

#include <stdint.h>

/* busterm's colours: background in the high nibble, the PC's numbers */
#define UI_FRAME 0x87  /* light grey on dark grey */
#define UI_TEXT 0x8F   /* white on dark grey */
#define UI_TITLE 0x8E  /* light cyan on dark grey */
#define UI_DIM 0x87    /* light grey on dark grey */
#define UI_PICKED 0x60 /* black on cyan */
#define UI_FIELD 0x0F  /* white on black */
#define UI_KEY 0x8B    /* yellow on dark grey */

typedef struct {
    uint8_t row, col, height, width;
} window_t;

/* opened, its inside cleared; 0 if there's no room to keep what's under */
uint8_t ui_open(window_t *w, uint8_t row, uint8_t col, uint8_t height, uint8_t width,
                const char *title);
void ui_close(window_t *w);

/* |text| at (row, col), then |pad| more cells filled with spaces, if any */
void ui_text(uint8_t row, uint8_t col, const char *text, uint8_t attr, uint8_t pad);
void ui_fill(uint8_t row, uint8_t col, uint8_t n, uint8_t c, uint8_t attr);

/* "Return dial  N new": a key in UI_KEY, its words in UI_DIM, at (row,
 * col); |hints| is key, words, key, words..., ended by 0 */
void ui_hints(uint8_t row, uint8_t col, const char *const *hints);

/* A question in a window of its own, |line1| and |line2| under it (or
 * 0): 1 for Y, 0 for N or Esc. */
uint8_t ui_ask(const char *line1, const char *line2);

/* A one-line text field at (row, col), |width| wide, editing |buf| (room
 * for |size| - 1 characters). Returns the key that left it: Return, Tab,
 * Escape, up or down. */
uint8_t ui_field(uint8_t row, uint8_t col, uint8_t width, char *buf, uint8_t size);

#endif
