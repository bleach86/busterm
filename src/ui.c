#include "ui.h"

#include "banks.h"
#include "keys.h"
#include "screen.h"
#include "sound.h"
#include "vera.h"

/* what's under the open windows, theirs one after another, in banked RAM:
 * bank BANK_WINDOWS, picked only while it's used */
#define KEEP_BYTES BANK_SIZE
#define keep BANKED
static uint16_t kept;

#define SHADOW 0x08 /* dark grey on black: a cell's character kept */

static void cells_out(uint8_t row, uint8_t col, uint8_t n, uint8_t *to) {
    vera_port1(screen_cell(row, col), VERA_INC1);
    n *= 2;
    while (n--) {
        *to++ = VERA_DATA1;
    }
}

static void cells_in(uint8_t row, uint8_t col, uint8_t n, const uint8_t *from) {
    vera_port1(screen_cell(row, col), VERA_INC1);
    n *= 2;
    while (n--) {
        VERA_DATA1 = *from++;
    }
}

void ui_fill(uint8_t row, uint8_t col, uint8_t n, uint8_t c, uint8_t attr) {
    vera_port1(screen_cell(row, col), VERA_INC1);
    while (n--) {
        VERA_DATA1 = c;
        VERA_DATA1 = attr;
    }
    screen_forget();
}

void ui_text(uint8_t row, uint8_t col, const char *text, uint8_t attr, uint8_t pad) {
    vera_port1(screen_cell(row, col), VERA_INC1);
    while (*text) {
        VERA_DATA1 = (uint8_t)*text++;
        VERA_DATA1 = attr;
    }
    while (pad--) {
        VERA_DATA1 = ' ';
        VERA_DATA1 = attr;
    }
    screen_forget();
}

static uint8_t length(const char *s) {
    uint8_t n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

void ui_hints(uint8_t row, uint8_t col, const char *const *hints) {
    while (*hints) {
        ui_text(row, col, hints[0], UI_KEY, 0);
        col += length(hints[0]);
        ui_text(row, col, hints[1], UI_DIM, 0);
        col += length(hints[1]);
        hints += 2;
    }
}

/* a shadow: the cells' characters kept, darkened */
static void shade(uint8_t row, uint8_t col, uint8_t n) {
    uint32_t at = screen_cell(row, col);
    while (n--) {
        vera_port1(at + 1, 0);
        VERA_DATA1 = SHADOW;
        at += 2;
    }
}

__attribute__((noinline)) uint8_t ui_open(window_t *w, uint8_t row, uint8_t col, uint8_t height,
                                          uint8_t width, const char *title) {
    /* the window and its shadow, a cell right and a row down */
    uint16_t need = (uint16_t)(height + 1) * (width + 1) * 2 + 4;
    uint8_t r, *to, bank;
    bell_stop(); /* the window's loop won't stop it */
    if (kept + need > KEEP_BYTES) {
        return 0;
    }
    screen_cursor_hide();
    w->row = row, w->col = col, w->height = height, w->width = width;
    bank = RAM_BANK;
    RAM_BANK = BANK_WINDOWS;
    to = keep + kept;
    for (r = 0; r <= height; ++r) {
        cells_out(row + r, col, width + 1, to);
        to += (width + 1) * 2;
    }
    RAM_BANK = bank;
    kept += need;

    /* the frame */
    ui_fill(row, col, 1, 0xDA, UI_FRAME); /* ┌ */
    ui_fill(row, col + 1, width - 2, 0xC4, UI_FRAME);
    ui_fill(row, col + width - 1, 1, 0xBF, UI_FRAME); /* ┐ */
    for (r = 1; r < height - 1; ++r) {
        ui_fill(row + r, col, 1, 0xB3, UI_FRAME); /* │ */
        ui_fill(row + r, col + 1, width - 2, ' ', UI_TEXT);
        ui_fill(row + r, col + width - 1, 1, 0xB3, UI_FRAME);
    }
    ui_fill(row + height - 1, col, 1, 0xC0, UI_FRAME); /* └ */
    ui_fill(row + height - 1, col + 1, width - 2, 0xC4, UI_FRAME);
    ui_fill(row + height - 1, col + width - 1, 1, 0xD9, UI_FRAME); /* ┘ */
    if (title) {
        uint8_t n = length(title);
        uint8_t at = col + (width - n - 2) / 2;
        ui_fill(row, at, 1, ' ', UI_TITLE);
        ui_text(row, at + 1, title, UI_TITLE, 1);
    }

    /* the shadow */
    for (r = 1; r <= height; ++r) {
        shade(row + r, col + width, 1);
    }
    shade(row + height, col + 1, width - 1);
    screen_forget();
    return 1;
}

__attribute__((noinline)) void ui_close(window_t *w) {
    uint16_t need = (uint16_t)(w->height + 1) * (w->width + 1) * 2 + 4;
    uint8_t r, bank = RAM_BANK;
    const uint8_t *from;
    kept -= need;
    RAM_BANK = BANK_WINDOWS;
    from = keep + kept;
    for (r = 0; r <= w->height; ++r) {
        cells_in(w->row + r, w->col, w->width + 1, from);
        from += (w->width + 1) * 2;
    }
    RAM_BANK = bank;
    screen_forget();
}

uint8_t ui_field(uint8_t row, uint8_t col, uint8_t width, char *buf, uint8_t size) {
    uint8_t n = length(buf);
    for (;;) {
        uint8_t k;
        /* the end of the text in view, and a cursor after it */
        uint8_t from = n >= width ? n - width + 1 : 0;
        ui_text(row, col, buf + from, UI_FIELD, width - (n - from));
        ui_fill(row, col + (n - from), 1, ' ', UI_PICKED);
        do {
            k = keys_get();
        } while (!k);
        if (k == KEY_RETURN || k == KEY_TAB || k == KEY_ESC || k == KEY_UP || k == KEY_DOWN) {
            ui_text(row, col, buf + from, UI_FIELD, width - (n - from));
            return k;
        }
        if (k == KEY_DEL) {
            if (n) {
                buf[--n] = 0;
            }
        } else if (k >= 0x20 && k != 0x7F && k < 0x80 && n < size - 1) {
            buf[n++] = (char)k;
            buf[n] = 0;
        }
    }
}

static const char *const ask_hints[] = {"Y", " yes   ", "N", " no", 0};

uint8_t ui_ask(const char *line1, const char *line2) {
    window_t w;
    uint8_t width = length(line1), k;
    if (line2 && length(line2) > width) {
        width = length(line2);
    }
    width += 8;
    if (width < 24) {
        width = 24;
    }
    if (!ui_open(&w, 11, (80 - width) / 2, line2 ? 8 : 7, width, 0)) {
        return 0;
    }
    ui_text(12, w.col + 4, line1, UI_TEXT, 0);
    if (line2) {
        ui_text(13, w.col + 4, line2, UI_TEXT, 0);
    }
    ui_hints(w.row + w.height - 2, w.col + 4, ask_hints);
    do {
        k = keys_get();
    } while (k != 'y' && k != 'Y' && k != 'n' && k != 'N' && k != KEY_ESC);
    ui_close(&w);
    return k == 'y' || k == 'Y';
}
