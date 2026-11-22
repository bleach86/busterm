#include "scrollback.h"

#include "banks.h"
#include "keys.h"
#include "screen.h"
#include "sound.h"
#include "vera.h"

/* a row as VRAM has it, 80 cells of character and colours; 51 to a bank */
#define ROW_BYTES 160
#define ROWS_A_BANK (BANK_SIZE / ROW_BYTES)
#define MOST ((uint16_t)(BANK_SCROLLBACK_END - BANK_SCROLLBACK) * ROWS_A_BANK)

static uint16_t count;  /* rows kept */
static uint16_t head;   /* where the next goes, 0 to MOST - 1 */
static uint8_t at_bank; /* and its bank */
static uint8_t *at;     /* and its place in it */
static uint8_t cells;   /* each row's width */

void scrollback_clear(void) {
    count = 0, head = 0;
    at_bank = BANK_SCROLLBACK;
    at = BANKED;
}

void scrollback_push(uint32_t addr, uint8_t width) {
    uint8_t bank = RAM_BANK, n = width * 2;
    uint8_t *to = at;
    cells = width;
    vera_port1(addr, VERA_INC1);
    RAM_BANK = at_bank;
    while (n--) {
        *to++ = VERA_DATA1;
    }
    RAM_BANK = bank;

    if (++head == MOST) {
        head = 0;
        at_bank = BANK_SCROLLBACK;
        at = BANKED;
    } else if ((at += ROW_BYTES) > BANKED + BANK_SIZE - ROW_BYTES) {
        ++at_bank;
        at = BANKED;
    }
    if (count < MOST) {
        ++count;
    }
}

/* row |line| of those kept, 0 the oldest, drawn on the view's row |row| */
static void draw_kept(uint8_t row, uint16_t line) {
    uint16_t i = head + MOST - count + line;
    uint8_t bank = RAM_BANK, n = cells * 2;
    const uint8_t *from;
    if (i >= MOST) {
        i -= MOST;
    }
    if (i >= MOST) {
        i -= MOST;
    }
    from = BANKED + (i % ROWS_A_BANK) * ROW_BYTES;
    vera_port1(screen_view_row(row), VERA_INC1);
    RAM_BANK = BANK_SCROLLBACK + i / ROWS_A_BANK;
    while (n--) {
        VERA_DATA1 = *from++;
    }
    RAM_BANK = bank;
}

/* the view, |back| rows up from the terminal: the kept rows, then the
 * terminal's own */
static void draw(uint16_t back) {
    uint8_t r;
    for (r = 0; r < screen_rows; ++r) {
        uint16_t line = count - back + r;
        if (line < count) {
            draw_kept(r, line);
        } else {
            screen_view_live(r, (uint8_t)(line - count));
        }
    }
}

static char *put_text(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

static char *put_number(char *p, uint16_t n) {
    char digits[5];
    uint8_t i = 0;
    do {
        digits[i++] = '0' + n % 10;
        n /= 10;
    } while (n);
    while (i) {
        *p++ = digits[--i];
    }
    return p;
}

/* the status line while looking: how far up, and the keys */
static void status(uint16_t back) {
    static char line[81];
    char *p;
    if (screen_cols == 40) {
        p = put_text(line, " back ");
        p = put_number(p, back);
        p = put_text(p, " of ");
        p = put_number(p, count);
        p = put_text(p, "  Esc done");
    } else {
        p = put_text(line, " scrollback \x10 ");
        p = put_number(p, back);
        p = put_text(p, " rows up of ");
        p = put_number(p, count);
        while (p < line + 34) {
            *p++ = ' ';
        }
        p = put_text(p, "    Up Down PgUp PgDn Home    Esc done");
    }
    *p = 0;
    screen_status(line);
}

void scrollback_view(uint8_t first) {
    uint16_t back = 0;
    uint8_t page = screen_rows - 1;
    uint8_t k = first, drawn = 0;

    screen_cursor_hide();
    screen_view(1);
    for (;;) {
        uint16_t was = back;
        if (k == KEY_UP) {
            back += back < count;
        } else if (k == KEY_DOWN) {
            back -= back > 0;
        } else if (k == KEY_PGUP) {
            back = count - back > page ? back + page : count;
        } else if (k == KEY_PGDN) {
            if (!back) {
                break; /* past the bottom: done */
            }
            back = back > page ? back - page : 0;
        } else if (k == KEY_HOME) {
            back = count;
        } else if (k == KEY_ESC || k == KEY_RETURN) {
            break;
        }
        if (back != was || !drawn) {
            draw(back);
            status(back);
            drawn = 1;
        }
        do {
            bell_tick();
            k = keys_alt();
            if (k == 'b') {
                k = KEY_ESC; /* Alt-B again */
            } else if (!k) {
                k = keys_get();
            }
        } while (!k);
    }
    screen_view(0);
}
