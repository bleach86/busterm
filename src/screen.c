#include "screen.h"

#include <cbm.h>
#include <cx16.h>

#include "font.h"
#include "vera.h"

#define L1_CONFIG (*(volatile uint8_t *)0x9F34)

/* the ROM's screen modes, and CHROUT's codes for its character sets */
#define MODE_80X30 1
#define MODE_40X30 3
#define ISO_MODE 0x0F     /* ISO-8859-15: the keyboard types ASCII */
#define PETSCII_MODE 0x8F /* PETSCII again */
#define LOWER_CASE 0x0E   /* PETSCII's lower and upper case set */
#define UPPER_CASE 0x8E   /* its upper case and graphics set */

#define ANSI_STATUS 0x60    /* black on cyan, the PC's colours */
#define PETSCII_STATUS 0x30 /* black on cyan, the C64's */
#define ANSI_START 0x07     /* light grey on black */
#define PETSCII_START 0x01  /* white on black */

uint8_t screen_cols, screen_rows;
uint8_t screen_petscii;
uint8_t screen_row, screen_col, screen_attr;
uint8_t screen_top, screen_bottom;
uint8_t screen_cursor_on = 1;

static uint32_t map_base; /* the text layer's map, in VRAM */
static uint8_t row_shift; /* a map row is 1 << row_shift bytes */
static uint8_t wrap_pending;
static uint8_t addr_ok; /* port 1 is at the cursor */
static uint8_t status_attr;

/* cursor drawn: where, and the cell as it was */
static uint8_t cursor_drawn, cursor_row, cursor_col, cursor_char, cursor_attr;

/* The PC's 16 colours (CGA), in the ANSI order, as VERA's palette has
 * them: green and blue in the first byte, red in the second, 4 bits each. */
static const uint8_t cga[32] = {
    0x00, 0x00, 0x00, 0x0A, 0xA0, 0x00, 0x50, 0x0A, /* black red green brown */
    0x0A, 0x00, 0x0A, 0x0A, 0xAA, 0x00, 0xAA, 0x0A, /* blue magenta cyan grey */
    0x55, 0x05, 0x55, 0x0F, 0xF5, 0x05, 0xF5, 0x0F, /* the same, bright */
    0x5F, 0x05, 0x5F, 0x0F, 0xFF, 0x05, 0xFF, 0x0F,
};

/* The C64's 16 colours, in its order, as the X16's own palette starts:
 * black white red cyan purple green blue yellow, orange brown light-red
 * dark-grey grey light-green light-blue light-grey. */
static const uint8_t c64[32] = {
    0x00, 0x00, 0xFF, 0x0F, 0x00, 0x08, 0xFE, 0x0A, 0x4C, 0x0C, 0xC5, 0x00, 0x0A, 0x00, 0xE7, 0x0E,
    0x85, 0x0D, 0x40, 0x06, 0x77, 0x0F, 0x33, 0x03, 0x77, 0x07, 0xF6, 0x0A, 0x8F, 0x00, 0xBB, 0x0B,
};

/* the screen's own rows: 0 the status line */
uint32_t screen_cell(uint8_t row, uint8_t col) {
    return map_base + ((uint32_t)row << row_shift) + (uint16_t)(col << 1);
}

/* the terminal's rows, under the status line */
static uint32_t cell(uint8_t row, uint8_t col) { return screen_cell(row + 1, col); }

/* port 1 at an address, interrupts already off */
static void port1_at(uint32_t addr, uint8_t inc) {
    VERA_CTRL = 1;
    VERA_ADDR_L = (uint8_t)addr;
    VERA_ADDR_M = (uint8_t)(addr >> 8);
    VERA_ADDR_H = (uint8_t)(addr >> 16) | inc;
    VERA_CTRL = 0;
}

static void palette(const uint8_t *colours) {
    uint8_t i;
    vera_port1(VERA_PALETTE, VERA_INC1);
    for (i = 0; i < 32; ++i) {
        VERA_DATA1 = colours[i];
    }
}

/* a screen mode, its map found, the terminal's size set, all cleared */
static void mode(uint8_t which, uint8_t cols, uint8_t rows) {
    uint8_t r;
    cx16_k_screen_mode_set(which);
    map_base = (uint32_t)VERA_L1_MAPBASE << 9;
    row_shift = 6 + ((L1_CONFIG >> 4) & 3); /* 32 to 256 tiles, 2 bytes each */
    screen_cols = cols, screen_rows = rows;
    screen_top = 0, screen_bottom = rows - 1;
    cursor_drawn = 0;
    /* the whole screen cleared, below the terminal's rows too */
    for (r = 0; r < 30; ++r) {
        vera_port1(screen_cell(r, 0), VERA_INC1);
        for (rows = 0; rows < cols; ++rows) {
            VERA_DATA1 = ' ';
            VERA_DATA1 = screen_attr;
        }
    }
    screen_goto(0, 0);
}

void screen_ansi(void) {
    uint16_t i;
    screen_petscii = 0;
    screen_attr = ANSI_START;
    status_attr = ANSI_STATUS;
    cbm_k_chrout(ISO_MODE);
    mode(MODE_80X30, 80, 29);
    palette(cga);
    vera_port1((uint32_t)(VERA_L1_TILEBASE & 0xFC) << 9, VERA_INC1);
    for (i = 0; i < sizeof font_cp437; ++i) {
        VERA_DATA1 = font_cp437[i];
    }
}

void screen_petscii_mode(uint8_t cols) {
    screen_petscii = 1;
    screen_attr = PETSCII_START;
    status_attr = PETSCII_STATUS;
    cbm_k_chrout(PETSCII_MODE);
    cbm_k_chrout(LOWER_CASE);
    mode(cols == 40 ? MODE_40X30 : MODE_80X30, cols, 25);
    palette(c64);
}

void screen_charset(uint8_t lower) { cbm_k_chrout(lower ? LOWER_CASE : UPPER_CASE); }

void screen_restore(void) {
    cbm_k_chrout(PETSCII_MODE);
    palette(c64);
    cx16_k_screen_mode_set(0);
}

void screen_goto(uint8_t row, uint8_t col) {
    screen_row = row < screen_rows ? row : screen_rows - 1;
    screen_col = col < screen_cols ? col : screen_cols - 1;
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
    } else if (screen_row < screen_rows - 1) {
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
    if (screen_col == screen_cols - 1) {
        /* ANSI waits for the next character; a Commodore wraps now */
        if (screen_petscii) {
            screen_col = 0;
            screen_lf();
        } else {
            wrap_pending = 1;
            addr_ok = 0;
        }
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
        screen_clear_cells(from, 0, screen_cols);
    }
}

/* |n| bytes copied in VRAM from |from| to |to|, both moving by |inc|;
 * interrupts off, as port 0 reads */
static void copy(uint32_t from, uint32_t to, uint8_t inc, uint8_t n) {
    vera_port0(from, inc);
    port1_at(to, inc);
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
        copy(cell(r + n, 0), cell(r, 0), VERA_INC1, screen_cols * 2);
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
        copy(cell(r - n, 0), cell(r, 0), VERA_INC1, screen_cols * 2);
    }
    irq_on();
    screen_clear_rows(top, top + n - 1);
}

void screen_delete_char(void) {
    uint8_t n = screen_cols - 1 - screen_col;
    if (n) {
        irq_off();
        copy(cell(screen_row, screen_col + 1), cell(screen_row, screen_col), VERA_INC1, n * 2);
        irq_on();
    }
    screen_clear_cells(screen_row, screen_cols - 1, 1);
}

void screen_insert_char(void) {
    uint8_t n = screen_cols - 1 - screen_col;
    if (n) {
        /* from the row's end backwards, a byte at a time */
        irq_off();
        copy(cell(screen_row, screen_cols - 2) + 1, cell(screen_row, screen_cols - 1) + 1,
             VERA_DEC1, n * 2);
        irq_on();
    }
    screen_clear_cells(screen_row, screen_col, 1);
}

void screen_cursor_show(void) {
    if (cursor_drawn || !screen_cursor_on) {
        return;
    }
    cursor_row = screen_row;
    cursor_col = wrap_pending ? screen_cols - 1 : screen_col;
    vera_port1(cell(cursor_row, cursor_col), 0);
    cursor_char = VERA_DATA1;
    vera_port1(cell(cursor_row, cursor_col) + 1, 0);
    cursor_attr = VERA_DATA1;
    if (screen_petscii) {
        /* a Commodore's cursor: the character reversed */
        vera_port1(cell(cursor_row, cursor_col), 0);
        VERA_DATA1 = cursor_char ^ 0x80;
    } else {
        VERA_DATA1 = (uint8_t)(cursor_attr << 4) | (uint8_t)(cursor_attr >> 4);
    }
    cursor_drawn = 1;
    addr_ok = 0;
}

void screen_cursor_hide(void) {
    if (!cursor_drawn) {
        return;
    }
    vera_port1(cell(cursor_row, cursor_col), VERA_INC1);
    VERA_DATA1 = cursor_char;
    VERA_DATA1 = cursor_attr;
    cursor_drawn = 0;
    addr_ok = 0;
}

void screen_forget(void) { addr_ok = 0; }

/* ASCII as a screen code: PETSCII's font, lower case set, has the
 * letters at 1 to 26 (lower case) and 65 to 90 (upper case) */
static uint8_t screen_code(uint8_t c) {
    if (!screen_petscii) {
        return c;
    }
    if (c >= 'a' && c <= 'z') {
        return c - 0x60;
    }
    if (c == 0x10) { /* ► */
        return '>';
    }
    return c;
}

void screen_status(const char *text) {
    uint8_t n = 0;
    vera_port1(screen_cell(0, 0), VERA_INC1);
    for (; n < screen_cols && *text; ++n) {
        VERA_DATA1 = screen_code((uint8_t)*text++);
        VERA_DATA1 = status_attr;
    }
    for (; n < screen_cols; ++n) {
        VERA_DATA1 = ' ';
        VERA_DATA1 = status_attr;
    }
    addr_ok = 0;
}
