#include "ansi.h"

#include "screen.h"
#include "uart.h"

/* letters as numbers: what comes from the modem is ASCII, whatever the
 * compiler's character set */
#define ESC 0x1B

enum { TEXT, ESCAPE, CSI };

#define MAX_PARAMS 8

static uint8_t state;
static uint8_t params[MAX_PARAMS];
static uint8_t nparams;
static uint8_t private_mark; /* ?, <, = or > after ESC [ */

/* SGR's state: the PC's colour numbers */
static uint8_t fg = 7, bg, bold, blink, reverse;

static uint8_t saved_row, saved_col;

static void set_attr(void) {
    uint8_t f = fg | (bold ? 8 : 0);
    uint8_t b = bg | (blink ? 8 : 0); /* blink as a bright background (iCE) */
    screen_attr = reverse ? (uint8_t)(f << 4) | b : (uint8_t)(b << 4) | f;
}

void ansi_reset(void) {
    state = TEXT;
    fg = 7, bg = 0, bold = 0, blink = 0, reverse = 0;
    set_attr();
    screen_top = 0;
    screen_bottom = SCREEN_ROWS - 1;
}

/* the first parameter, or |otherwise| when it's missing or 0 */
static uint8_t param(uint8_t i, uint8_t otherwise) {
    return i < nparams && params[i] ? params[i] : otherwise;
}

static void reply_number(uint8_t n) {
    if (n >= 100) {
        uart_put('0' + n / 100);
    }
    if (n >= 10) {
        uart_put('0' + n / 10 % 10);
    }
    uart_put('0' + n % 10);
}

/* where the cursor is: ESC [ row ; col R */
static void reply_position(void) {
    uart_put(ESC);
    uart_put('[');
    reply_number(screen_row + 1);
    uart_put(';');
    reply_number(screen_col + 1);
    uart_put(0x52 /* R */);
}

static void sgr(void) {
    uint8_t i;
    if (!nparams) {
        params[0] = 0;
        nparams = 1;
    }
    for (i = 0; i < nparams; ++i) {
        uint8_t p = params[i];
        if (p == 0) {
            fg = 7, bg = 0, bold = 0, blink = 0, reverse = 0;
        } else if (p == 1) {
            bold = 1;
        } else if (p == 5 || p == 6) {
            blink = 1;
        } else if (p == 7) {
            reverse = 1;
        } else if (p == 22) {
            bold = 0;
        } else if (p == 25) {
            blink = 0;
        } else if (p == 27) {
            reverse = 0;
        } else if (p >= 30 && p <= 37) {
            fg = p - 30;
        } else if (p == 39) {
            fg = 7;
        } else if (p >= 40 && p <= 47) {
            bg = p - 40;
        } else if (p == 49) {
            bg = 0;
        } else if (p >= 90 && p <= 97) {
            fg = p - 90, bold = 1;
        } else if (p >= 100 && p <= 107) {
            bg = p - 100, blink = 1;
        } else if (p == 38 || p == 48) {
            /* 256 colours or RGB: skipped, with their numbers */
            i += (i + 1 < nparams && params[i + 1] == 2) ? 4 : 2;
        }
    }
    set_attr();
}

static void erase_display(uint8_t how) {
    if (how == 0) { /* cursor to end */
        screen_clear_cells(screen_row, screen_col, SCREEN_COLS - screen_col);
        if (screen_row < SCREEN_ROWS - 1) {
            screen_clear_rows(screen_row + 1, SCREEN_ROWS - 1);
        }
    } else if (how == 1) { /* start to cursor */
        if (screen_row) {
            screen_clear_rows(0, screen_row - 1);
        }
        screen_clear_cells(screen_row, 0, screen_col + 1);
    } else { /* all, and home, as ANSI.SYS does */
        screen_clear_rows(0, SCREEN_ROWS - 1);
        screen_goto(0, 0);
    }
}

static void erase_line(uint8_t how) {
    if (how == 0) {
        screen_clear_cells(screen_row, screen_col, SCREEN_COLS - screen_col);
    } else if (how == 1) {
        screen_clear_cells(screen_row, 0, screen_col + 1);
    } else {
        screen_clear_cells(screen_row, 0, SCREEN_COLS);
    }
}

static void csi(uint8_t final) {
    uint8_t n = param(0, 1);
    switch (final) {
    case 0x41: /* A up */
        screen_goto(screen_row > n ? screen_row - n : 0, screen_col);
        break;
    case 0x42: /* B down */
        screen_goto(screen_row + n, screen_col);
        break;
    case 0x43: /* C right */
        screen_goto(screen_row,
                    screen_col + n > SCREEN_COLS - 1 ? SCREEN_COLS - 1 : screen_col + n);
        break;
    case 0x44: /* D left */
        screen_goto(screen_row, screen_col > n ? screen_col - n : 0);
        break;
    case 0x45: /* E next line */
        screen_goto(screen_row + n, 0);
        break;
    case 0x46: /* F previous line */
        screen_goto(screen_row > n ? screen_row - n : 0, 0);
        break;
    case 0x47: /* G column */
        screen_goto(screen_row, n - 1);
        break;
    case 0x64: /* d row */
        screen_goto(n - 1, screen_col);
        break;
    case 0x48: /* H position */
    case 0x66: /* f */
        screen_goto(param(0, 1) - 1, param(1, 1) - 1);
        break;
    case 0x4A: /* J erase display */
        erase_display(nparams ? params[0] : 0);
        break;
    case 0x4B: /* K erase line */
        erase_line(nparams ? params[0] : 0);
        break;
    case 0x58: /* X erase characters */
        screen_clear_cells(screen_row, screen_col,
                           screen_col + n > SCREEN_COLS ? SCREEN_COLS - screen_col : n);
        break;
    case 0x4C: /* L insert lines */
        if (screen_row >= screen_top && screen_row <= screen_bottom) {
            screen_scroll_down(screen_row, screen_bottom, n);
        }
        break;
    case 0x4D: /* M delete lines */
        if (screen_row >= screen_top && screen_row <= screen_bottom) {
            screen_scroll_up(screen_row, screen_bottom, n);
        }
        break;
    case 0x53: /* S scroll up */
        screen_scroll_up(screen_top, screen_bottom, n);
        break;
    case 0x54: /* T scroll down */
        screen_scroll_down(screen_top, screen_bottom, n);
        break;
    case 0x6D: /* m colours */
        sgr();
        break;
    case 0x72: /* r scrolling region */
    {
        uint8_t top = param(0, 1) - 1, bottom = param(1, SCREEN_ROWS) - 1;
        if (bottom >= SCREEN_ROWS) {
            bottom = SCREEN_ROWS - 1;
        }
        if (top < bottom) {
            screen_top = top, screen_bottom = bottom;
        }
        screen_goto(0, 0);
    } break;
    case 0x73: /* s save */
        saved_row = screen_row, saved_col = screen_col;
        break;
    case 0x75: /* u restore */
        screen_goto(saved_row, saved_col);
        break;
    case 0x6E: /* n: the board asks */
        if (nparams && params[0] == 5) {
            uart_puts("\x1b[0n"); /* all's well */
        } else if (nparams && (params[0] == 6 || params[0] == 255)) {
            /* 6 where the cursor is; 255 (SyncTERM's) the screen's size */
            if (params[0] == 255) {
                uint8_t row = screen_row, col = screen_col;
                screen_row = SCREEN_ROWS - 1, screen_col = SCREEN_COLS - 1;
                reply_position();
                screen_row = row, screen_col = col;
            } else {
                reply_position();
            }
        }
        break;
    case 0x63: /* c what are you: a VT100 */
        if (!private_mark) {
            uart_puts("\x1b[?1;0c");
        }
        break;
    case 0x68: /* h and l: the cursor shown or hidden */
    case 0x6C:
        if (private_mark == '?' && nparams && params[0] == 25) {
            screen_cursor_on = final == 0x68;
        }
        break;
    default:
        break;
    }
}

void ansi_feed(uint8_t c) {
    if (state == TEXT) {
        if (c >= 0x20) {
            screen_put(c);
        } else if (c == ESC) {
            state = ESCAPE;
        } else if (c == 0x0D) {
            screen_cr();
        } else if (c == 0x0A || c == 0x0B || c == 0x0C) {
            screen_lf();
        } else if (c == 0x08) {
            if (screen_col) {
                screen_goto(screen_row, screen_col - 1);
            }
        } else if (c == 0x09) {
            screen_goto(screen_row, (screen_col | 7) + 1 > SCREEN_COLS - 1 ? SCREEN_COLS - 1
                                                                           : (screen_col | 7) + 1);
        }
        /* other control codes, BEL among them, draw nothing */
        return;
    }

    if (state == ESCAPE) {
        state = TEXT;
        if (c == '[') {
            state = CSI;
            nparams = 0;
            params[0] = 0;
            private_mark = 0;
        } else if (c == 0x37) { /* 7 save */
            saved_row = screen_row, saved_col = screen_col;
        } else if (c == 0x38) { /* 8 restore */
            screen_goto(saved_row, saved_col);
        } else if (c == 0x44) { /* D index */
            screen_lf();
        } else if (c == 0x45) { /* E next line */
            screen_cr();
            screen_lf();
        } else if (c == 0x4D) { /* M reverse index */
            screen_reverse_lf();
        } else if (c == 0x63) { /* c reset */
            ansi_reset();
            erase_display(2);
        }
        return;
    }

    /* CSI: numbers, ';', a private mark, intermediates, then the final */
    if (c >= '0' && c <= '9') {
        if (!nparams) {
            nparams = 1;
        }
        {
            uint16_t v = params[nparams - 1] * 10 + (c - '0');
            params[nparams - 1] = v > 255 ? 255 : (uint8_t)v;
        }
    } else if (c == ';') {
        if (!nparams) {
            nparams = 1;
        }
        if (nparams < MAX_PARAMS) {
            params[nparams++] = 0;
        }
    } else if (c >= 0x3C && c <= 0x3F) {
        private_mark = c;
    } else if (c >= 0x20 && c <= 0x2F) {
        /* intermediates (SyncTERM's font and speed codes): left out */
    } else if (c >= 0x40 && c <= 0x7E) {
        state = TEXT;
        csi(c);
    } else {
        state = TEXT; /* a broken sequence */
    }
}
