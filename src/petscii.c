#include "petscii.h"

#include "screen.h"

static uint8_t reverse;

void petscii_reset(void) {
    reverse = 0;
    screen_attr = 0x01; /* white on black */
}

/* the colour codes and the C64's colour each sets */
static const uint8_t colour_codes[16] = {
    0x90, /* black */
    0x05, /* white */
    0x1C, /* red */
    0x9F, /* cyan */
    0x9C, /* purple */
    0x1E, /* green */
    0x1F, /* blue */
    0x9E, /* yellow */
    0x81, /* orange */
    0x95, /* brown */
    0x96, /* light red */
    0x97, /* dark grey */
    0x98, /* grey */
    0x99, /* light green */
    0x9A, /* light blue */
    0x9B, /* light grey */
};

/* a printable PETSCII code's screen code, as the C64's ROM finds it */
static uint8_t screen_code(uint8_t c) {
    if (c < 0x40) {
        return c; /* space, digits, punctuation */
    }
    if (c < 0x60) {
        return c - 0x40; /* @ and the letters */
    }
    if (c < 0x80) {
        return c - 0x20; /* the shifted letters and graphics */
    }
    if (c < 0xC0) {
        return c - 0x40; /* 0xA0 to 0xBF: graphics */
    }
    if (c == 0xFF) {
        return 0x5E; /* pi */
    }
    return c - 0x80; /* 0xC0 to 0xFE: the shifted ones again */
}

static void left(void) {
    if (screen_col) {
        screen_goto(screen_row, screen_col - 1);
    } else if (screen_row) {
        screen_goto(screen_row - 1, screen_cols - 1);
    }
}

static void right(void) {
    if (screen_col < screen_cols - 1) {
        screen_goto(screen_row, screen_col + 1);
    } else {
        screen_cr();
        screen_lf();
    }
}

void petscii_feed(uint8_t c) {
    uint8_t i;

    if ((c >= 0x20 && c < 0x80) || c >= 0xA0) {
        screen_put(screen_code(c) | (reverse ? 0x80 : 0));
        return;
    }

    for (i = 0; i < 16; ++i) {
        if (colour_codes[i] == c) {
            screen_attr = i; /* on black */
            return;
        }
    }

    switch (c) {
    case 0x0D: /* RETURN */
    case 0x8D: /* shifted RETURN */
        screen_cr();
        screen_lf();
        reverse = 0;
        break;
    case 0x11: /* down */
        screen_lf();
        break;
    case 0x91: /* up */
        if (screen_row) {
            screen_goto(screen_row - 1, screen_col);
        }
        break;
    case 0x1D: /* right */
        right();
        break;
    case 0x9D: /* left */
        left();
        break;
    case 0x13: /* HOME */
        screen_goto(0, 0);
        break;
    case 0x93: /* CLR */
        screen_clear_rows(0, screen_rows - 1);
        screen_goto(0, 0);
        break;
    case 0x12: /* reverse on */
        reverse = 1;
        break;
    case 0x92: /* reverse off */
        reverse = 0;
        break;
    case 0x14: /* DEL: back one, the rest of the row closing up */
        if (screen_col || screen_row) {
            left();
            screen_delete_char();
        }
        break;
    case 0x94: /* INST */
        screen_insert_char();
        break;
    case 0x0E: /* lower and upper case */
        screen_charset(1);
        break;
    case 0x8E: /* upper case and graphics */
        screen_charset(0);
        break;
    default:
        /* the rest (bell, the charset lock, function keys) draw nothing */
        break;
    }
}
