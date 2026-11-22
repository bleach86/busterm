#include "screen.h"

#include <cbm.h>
#include <cx16.h>

#include "font.h"
#include "vera.h"

#define L1_CONFIG (*(volatile uint8_t *)0x9F34)

#define MODE_80X30 1
#define ISO_MODE 0x0F
#define PETSCII_MODE 0x8F

#define STATUS_ATTR 0x60 /* black on cyan */
#define START_ATTR 0x07  /* light grey on black */

uint8_t screen_row, screen_col, screen_attr = START_ATTR;
uint8_t screen_top, screen_bottom = SCREEN_ROWS - 1;
uint8_t screen_cursor_on = 1;

static uint32_t map_base; /* the text layer's map, in VRAM */
static uint8_t row_shift; /* a map row is 1 << row_shift bytes */
static uint8_t wrap_pending;
static uint8_t addr_ok; /* port 1 is at the cursor */

/* cursor drawn: where, and the cell's colours before */
static uint8_t cursor_drawn, cursor_row, cursor_col, cursor_attr;

/* The PC's 16 colours (CGA), in the ANSI order, as VERA's palette has
 * them: green and blue in the first byte, red in the second, 4 bits each. */
static const uint8_t cga[32] = {
    0x00, 0x00, 0x00, 0x0A, 0xA0, 0x00, 0x50, 0x0A, /* black red green brown */
    0x0A, 0x00, 0x0A, 0x0A, 0xAA, 0x00, 0xAA, 0x0A, /* blue magenta cyan grey */
    0x55, 0x05, 0x55, 0x0F, 0xF5, 0x05, 0xF5, 0x0F, /* the same, bright */
    0x5F, 0x05, 0x5F, 0x0F, 0xFF, 0x05, 0xFF, 0x0F,
};

/* the screen's own rows: 0 the status line */
uint32_t screen_cell(uint8_t row, uint8_t col) {
    return map_base + ((uint32_t)row << row_shift) + (uint16_t)(col << 1);
}

/* the terminal's rows, under the status line */
static uint32_t cell(uint8_t row, uint8_t col) { return screen_cell(row + 1, col); }

/* port 1 at a cell, interrupts already off */
static void port1_at(uint32_t addr) {
    VERA_CTRL = 1;
    VERA_ADDR_L = (uint8_t)addr;
    VERA_ADDR_M = (uint8_t)(addr >> 8);
    VERA_ADDR_H = (uint8_t)(addr >> 16) | VERA_INC1;
    VERA_CTRL = 0;
}

void screen_init(void) {
    uint16_t i;
    uint8_t width;

    cx16_k_screen_mode_set(MODE_80X30);
    cbm_k_chrout(ISO_MODE); /* the keyboard's codes ISO-8859-15, not PETSCII */

    map_base = (uint32_t)VERA_L1_MAPBASE << 9;
    width = (L1_CONFIG >> 4) & 3; /* 32, 64, 128 or 256 tiles */
    row_shift = 6 + width;        /* two bytes a tile */

    vera_port1(VERA_PALETTE, VERA_INC1);
    for (i = 0; i < sizeof cga; ++i) {
        VERA_DATA1 = cga[i];
    }
    vera_port1((uint32_t)(VERA_L1_TILEBASE & 0xFC) << 9, VERA_INC1);
    for (i = 0; i < sizeof font_cp437; ++i) {
        VERA_DATA1 = font_cp437[i];
    }

    screen_attr = START_ATTR;
    screen_clear_rows(0, SCREEN_ROWS - 1);
    screen_goto(0, 0);
}

void screen_restore(void) {
    cbm_k_chrout(PETSCII_MODE);
    cx16_k_screen_mode_set(0);
}

void screen_goto(uint8_t row, uint8_t col) {
    screen_row = row < SCREEN_ROWS ? row : SCREEN_ROWS - 1;
    screen_col = col < SCREEN_COLS ? col : SCREEN_COLS - 1;
    wrap_pending = 0;
    addr_ok = 0;
}

void screen_cr(void) {
    screen_col = 0;
    wrap_pending = 0;
    addr_ok = 0;
}

void screen_lf(void) {
    if (screen_row == screen_bottom) {
        screen_scroll_up(screen_top, screen_bottom, 1);
    } else if (screen_row < SCREEN_ROWS - 1) {
        ++screen_row;
    }
    wrap_pending = 0;
    addr_ok = 0;
}

void screen_reverse_lf(void) {
    if (screen_row == screen_top) {
        screen_scroll_down(screen_top, screen_bottom, 1);
    } else if (screen_row) {
        --screen_row;
    }
    wrap_pending = 0;
    addr_ok = 0;
}

void screen_put(uint8_t c) {
    if (wrap_pending) {
        screen_col = 0;
        screen_lf();
    }
    if (!addr_ok) {
        vera_port1(cell(screen_row, screen_col), VERA_INC1);
        addr_ok = 1;
    }
    VERA_DATA1 = c;
    VERA_DATA1 = screen_attr;
    if (screen_col == SCREEN_COLS - 1) {
        wrap_pending = 1;
        addr_ok = 0;
    } else {
        ++screen_col;
    }
}

void screen_clear_cells(uint8_t row, uint8_t col, uint8_t n) {
    vera_port1(cell(row, col), VERA_INC1);
    while (n--) {
        VERA_DATA1 = ' ';
        VERA_DATA1 = screen_attr;
    }
    addr_ok = 0;
}

void screen_clear_rows(uint8_t from, uint8_t to) {
    for (; from <= to; ++from) {
        screen_clear_cells(from, 0, SCREEN_COLS);
    }
}

/* a row's cells copied to another row: interrupts off, port 0 reads */
static void copy_row(uint8_t from, uint8_t to) {
    uint8_t n = SCREEN_COLS * 2;
    vera_port0(cell(from, 0), VERA_INC1);
    port1_at(cell(to, 0));
    while (n--) {
        VERA_DATA1 = VERA_DATA0;
    }
}

void screen_scroll_up(uint8_t top, uint8_t bottom, uint8_t n) {
    uint8_t r;
    if (n > bottom - top) {
        screen_clear_rows(top, bottom);
        return;
    }
    irq_off();
    for (r = top; r + n <= bottom; ++r) {
        copy_row(r + n, r);
    }
    irq_on();
    screen_clear_rows(bottom - n + 1, bottom);
}

void screen_scroll_down(uint8_t top, uint8_t bottom, uint8_t n) {
    uint8_t r;
    if (n > bottom - top) {
        screen_clear_rows(top, bottom);
        return;
    }
    irq_off();
    for (r = bottom; r >= top + n; --r) {
        copy_row(r - n, r);
    }
    irq_on();
    screen_clear_rows(top, top + n - 1);
}

void screen_cursor_show(void) {
    uint32_t at;
    if (cursor_drawn || !screen_cursor_on) {
        return;
    }
    cursor_row = screen_row;
    cursor_col = wrap_pending ? SCREEN_COLS - 1 : screen_col;
    at = cell(cursor_row, cursor_col) + 1;
    vera_port1(at, 0);
    cursor_attr = VERA_DATA1;
    VERA_DATA1 = (uint8_t)(cursor_attr << 4) | (uint8_t)(cursor_attr >> 4);
    cursor_drawn = 1;
    addr_ok = 0;
}

void screen_cursor_hide(void) {
    if (!cursor_drawn) {
        return;
    }
    vera_port1(cell(cursor_row, cursor_col) + 1, 0);
    VERA_DATA1 = cursor_attr;
    cursor_drawn = 0;
    addr_ok = 0;
}

void screen_forget(void) { addr_ok = 0; }

void screen_status(const char *text) {
    uint8_t n = 0;
    vera_port1(screen_cell(0, 0), VERA_INC1);
    for (; n < SCREEN_COLS && *text; ++n) {
        VERA_DATA1 = (uint8_t)*text++;
        VERA_DATA1 = STATUS_ATTR;
    }
    for (; n < SCREEN_COLS; ++n) {
        VERA_DATA1 = ' ';
        VERA_DATA1 = STATUS_ATTR;
    }
    addr_ok = 0;
}
