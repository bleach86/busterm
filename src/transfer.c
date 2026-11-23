#include "transfer.h"

#include <cbm.h>
#include <cx16.h>

#include "banks.h"
#include "files.h"
#include "keys.h"
#include "screen.h"
#include "uart.h"
#include "ui.h"
#include "xfer.h"

#define SOH 0x01 /* a block of 128 */
#define STX 0x02 /* a block of 1024 */
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define WANT_CRC 'C'
#define WANT_STREAM 'G' /* YMODEM-g: blocks one after another, no ACKs */

#define START_TRIES 10 /* 'C' (or NAK) every 3 seconds, while nothing comes */
#define CRC_TRIES 4    /* then XMODEM's checksum asked for instead */
#define MOST_ERRORS 10

/* in banked RAM: the block, and the CRC's table in two halves */
#define BLOCK BANKED
#define CRC_HI (BANKED + 0x400)
#define CRC_LO (BANKED + 0x500)

/* what came, as a block's looked for */
enum { GOT_BLOCK, GOT_BAD, GOT_NOTHING, GOT_EOT, GOT_CAN, GOT_STOP, GOT_NONE };

static void (*say)(const char *);
static uint8_t use_crc, ymodem, streaming;
uint8_t zmodem, sending;
static uint8_t blk;   /* the block's number */
static uint16_t len;  /* and length */
char name[41];        /* the file's */
static char spec[52]; /* and what the drive's told */
static uint8_t one_k;
unsigned long size, got; /* size: 0 if not known (XMODEM) */
static unsigned long shown_at;

static void tables(void) {
    uint8_t i = 0, j;
    do {
        uint16_t crc = (uint16_t)i << 8;
        for (j = 0; j < 8; ++j) {
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
        }
        CRC_HI[i] = (uint8_t)(crc >> 8);
        CRC_LO[i] = (uint8_t)crc;
    } while (++i);
}

/* a byte from the board within |wait| jiffies; or TIMEOUT, or STOPPED */
uint16_t get(uint16_t wait) {
    unsigned long until;
    if (uart_ready()) {
        return uart_get();
    }
    until = cx16_k_rdtim() + wait;
    for (;;) {
        if (uart_ready()) {
            return uart_get();
        }
        if (keys_get() == KEY_ESC || !uart_carrier()) {
            return STOPPED;
        }
        if (cx16_k_rdtim() >= until) {
            return TIMEOUT;
        }
    }
}

/* a byte thrown away, if one comes within |wait| jiffies */
uint8_t uart_get_within(uint16_t wait) {
    unsigned long until = cx16_k_rdtim() + wait;
    do {
        if (uart_ready()) {
            uart_get();
            return 1;
        }
    } while (uart_carrier() && cx16_k_rdtim() < until);
    return 0;
}

/* what's coming thrown away, till a second's quiet */
static void purge(void) {
    while (get(SECOND) < 0x100) {
    }
}

/* a block, or what came instead */
static uint8_t block(uint16_t wait) {
    uint16_t c = get(wait), n;
    uint8_t nblk, hi = 0, lo = 0, sum = 0, idx, *p = BLOCK;

    if (c == STOPPED) {
        return GOT_STOP;
    }
    if (c == TIMEOUT) {
        return GOT_NOTHING;
    }
    if (c == EOT) {
        return GOT_EOT;
    }
    if (c == CAN) {
        return get(SECOND) == CAN ? GOT_CAN : GOT_BAD;
    }
    if (c != SOH && c != STX) {
        return GOT_BAD;
    }
    len = c == STX ? 1024 : 128;
    one_k |= c == STX;
    if ((c = get(SECOND)) > 0xFF) {
        return c == STOPPED ? GOT_STOP : GOT_BAD;
    }
    blk = (uint8_t)c;
    if ((c = get(SECOND)) > 0xFF) {
        return c == STOPPED ? GOT_STOP : GOT_BAD;
    }
    nblk = (uint8_t)c;
    for (n = len; n; --n) {
        if (uart_ready()) {
            c = uart_get();
        } else if ((c = get(SECOND)) > 0xFF) {
            return c == STOPPED ? GOT_STOP : GOT_BAD;
        }
        *p++ = (uint8_t)c;
        sum += (uint8_t)c;
        idx = hi ^ (uint8_t)c;
        hi = lo ^ CRC_HI[idx];
        lo = CRC_LO[idx];
    }
    if ((c = get(SECOND)) > 0xFF) {
        return c == STOPPED ? GOT_STOP : GOT_BAD;
    }
    if (use_crc) {
        uint16_t c2 = get(SECOND);
        if (c2 > 0xFF) {
            return c2 == STOPPED ? GOT_STOP : GOT_BAD;
        }
        if ((uint8_t)c != hi || (uint8_t)c2 != lo) {
            return GOT_BAD;
        }
    } else if ((uint8_t)c != sum) {
        return GOT_BAD;
    }
    return (uint8_t)(blk ^ nblk) == 0xFF ? GOT_BLOCK : GOT_BAD;
}

char *put_text(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

char *put_number(char *p, unsigned long n) {
    char digits[10];
    uint8_t i = 0;
    do {
        digits[i++] = '0' + (uint8_t)(n % 10);
        n /= 10;
    } while (n);
    while (i) {
        *p++ = digits[--i];
    }
    return p;
}

/* the status line: the file and how far it's got, at most 4 times a
 * second unless |now| */
/* The transfer's window, over the terminal on an ANSI screen: the file,
 * how much of it, a bar, and how to stop. It's closed before a line's said
 * (tell), which would scroll the terminal under it, and opened again by
 * the next progress. On a PETSCII screen, the status line instead. */
#define TW_ROW 10
#define TW_COL 14
#define TW_HEIGHT 10
#define TW_WIDTH 52
#define BAR 46
static window_t tw;
static uint8_t tw_open;
static const char *const stop_hints[] = {"Esc", " stops", 0};

static void window_shut(void) {
    if (tw_open) {
        ui_close(&tw);
        tw_open = 0;
    }
}

/* the protocol, as it's going */
static char *put_protocol(char *p) {
    p = put_text(p, zmodem ? "ZMODEM" : ymodem ? "YMODEM" : "XMODEM");
    return put_text(p, zmodem ? "" : streaming ? "-g" : one_k ? "-1K" : use_crc ? "-CRC" : "");
}

/* in BANK_CODE, with the picker's window, for main RAM's room: 1 if it's
 * shown, 0 if there's no room for it */
IN_BANK static uint8_t window_shown(void) {
    static char line[48];
    char *p;
    uint8_t filled = 0;
    if (!tw_open) {
        if (!(tw_open = ui_open(&tw, TW_ROW, TW_COL, TW_HEIGHT, TW_WIDTH,
                                sending ? "Sending" : "Receiving"))) {
            return 0;
        }
        ui_text(TW_ROW + 2, TW_COL + 3, "file", UI_DIM, 0);
        ui_text(TW_ROW + 3, TW_COL + 3, "bytes", UI_DIM, 0);
        ui_hints(TW_ROW + TW_HEIGHT - 2, TW_COL + 3, stop_hints);
    }
    ui_text(TW_ROW + 2, TW_COL + 10, name[0] ? name : "...", UI_TEXT, 0);
    p = put_number(line, got);
    if (size) {
        p = put_text(p, " of ");
        p = put_number(p, size);
    }
    p = put_text(p, "       ");
    *p = 0;
    ui_text(TW_ROW + 3, TW_COL + 10, line, UI_TEXT, 0);
    /* the bar: how much of it, if its size is known */
    if (size) {
        filled = got >= size ? BAR : (uint8_t)(got * BAR / size);
    }
    ui_fill(TW_ROW + 5, TW_COL + 3, filled, 0xDB, UI_TITLE);              /* █ */
    ui_fill(TW_ROW + 5, TW_COL + 3 + filled, BAR - filled, 0xB0, UI_DIM); /* ░ */
    *put_protocol(line) = 0;
    ui_text(TW_ROW + TW_HEIGHT - 2, TW_COL + TW_WIDTH - 15, line, UI_DIM, 0);
    return 1;
}

static uint8_t far8(uint8_t (*code)(void), uint8_t without);

void progress(uint8_t now) {
    static char line[81];
    char *p = line;
    unsigned long t = cx16_k_rdtim();
    if (!now && t - shown_at < SECOND / 4) {
        return;
    }
    shown_at = t;
    if (!screen_petscii && far8(window_shown, 0)) {
        return;
    }
    if (screen_cols == 40) {
        uint8_t i;
        *p++ = ' ';
        for (i = 0; i < 18 && name[i]; ++i) {
            *p++ = name[i];
        }
        *p++ = ' ';
        p = put_number(p, got);
        if (size) {
            *p++ = '/';
            p = put_number(p, size);
        }
    } else {
        p = put_text(p, sending ? " sending " : " receiving ");
        p = put_text(p, name[0] ? name : "...");
        *p++ = ' ', *p++ = ' ';
        p = put_number(p, got);
        if (size) {
            p = put_text(p, " of ");
            p = put_number(p, size);
        }
        p = put_text(p, " bytes  ");
        p = put_protocol(p);
        p = put_text(p, "   Esc stops");
    }
    *p = 0;
    screen_status(line);
}

/* The board told to stop, and what it had sent already thrown away, till
 * a second's quiet (or 20 seconds, if it won't stop), so the terminal
 * isn't filled with it. Then told again: a receiver throws away what came
 * before it ACKs a block, CANs and all, and then waits. */
void cancel(void) {
    unsigned long until = cx16_k_rdtim() + 20 * SECOND;
    uint8_t i;
    do {
        for (i = 0; i < 8; ++i) {
            uart_put(CAN);
        }
        for (i = 0; i < 255 && uart_get_within(SECOND); ++i) {
        }
    } while (i == 255 && cx16_k_rdtim() < until);
    for (i = 0; i < 8; ++i) {
        uart_put(CAN);
    }
}

/* |s| and the file's name, said */
void tell(const char *s, uint8_t with_bytes) {
    static char line[96];
    char *p = put_text(line, "[");
    p = put_text(p, s);
    if (name[0]) {
        *p++ = ' ';
        p = put_text(p, name);
    }
    if (with_bytes) {
        p = put_text(p, ", ");
        p = put_number(p, got);
        p = put_text(p, " bytes");
    }
    *p++ = ']';
    *p = 0;
    window_shut();
    say(line);
}

/* the file being written left half done: told why, and gone */
void fail(const char *why) {
    file_close();
    put_text(put_text(spec, "S:"), name)[0] = 0;
    file_command(spec);
    tell(why, 0);
}

/* a file name a board gave, safe for the drive: no folders, nothing the
 * drive's commands use */
static void take_name(const char *s) {
    const char *from = s;
    uint8_t n = 0;
    for (; *s; ++s) {
        if (*s == '/' || *s == '\\') {
            from = s + 1;
        }
    }
    for (; *from && n < sizeof name - 1; ++from) {
        char c = *from;
        if (c <= ' ' || c > '~' || c == ',' || c == ':' || c == '*' || c == '?' || c == '=' ||
            c == '@' || c == '$' || c == '"') {
            c = '_';
        }
        name[n++] = c;
    }
    name[n] = 0;
    if (!n) {
        put_text(name, "DOWNLOAD")[0] = 0;
    }
}

/* the file made, over any of the same name */
uint8_t create(void) {
    char *p = put_text(spec, "@:");
    p = put_text(p, name);
    put_text(p, ",S,W")[0] = 0;
    got = 0;
    return file_create(spec);
}

/* the size in a YMODEM or ZFILE header at |header|, after the name */
static unsigned long header_size(const uint8_t *header) {
    const uint8_t *p = header;
    unsigned long n = 0;
    while (*p && p < header + 127) {
        ++p;
    }
    ++p;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p++ - '0');
    }
    return n;
}

/* the files, one after another (YMODEM's), till the board's done */
static void receive(const char *xname) {
    uint8_t tries, errors, r, expect;

    ymodem = 0, one_k = 0, streaming = 0;
    for (;;) {
        name[0] = 0, size = 0, got = 0;
        progress(1);

        /* The start asked for, till a block comes: YMODEM-g first, which
         * a YMODEM sender knows and an XMODEM one may not, then CRC, then
         * the checksum. */
        use_crc = 1;
        for (tries = 0;; ++tries) {
            uint8_t want;
            if (tries == START_TRIES) {
                if (!ymodem) {
                    tell("nothing came", 0);
                }
                return;
            }
            if (tries == CRC_TRIES && !ymodem) {
                use_crc = 0;
            }
            want = ymodem    ? (streaming ? WANT_STREAM : WANT_CRC)
                   : !tries  ? WANT_STREAM
                   : use_crc ? WANT_CRC
                             : NAK;
            uart_put(want);
            r = block(3 * SECOND);
            if (r == GOT_BLOCK) {
                if (!ymodem) {
                    streaming = want == WANT_STREAM && blk == 0;
                }
                break;
            } else if (r == GOT_STOP) {
                cancel();
                tell("stopped", 0);
                return;
            } else if (r == GOT_CAN) {
                tell("the board stopped", 0);
                return;
            } else if (r == GOT_BAD) {
                purge();
            }
        }

        if (blk == 0) {
            /* YMODEM's header: the name and size, or none, the end */
            ymodem = 1;
            if (!BLOCK[0]) {
                uart_put(ACK);
                return;
            }
            BLOCK[127] = 0;
            take_name((const char *)BLOCK);
            size = header_size(BLOCK);
            if (!create()) {
                cancel();
                tell("couldn't make", 0);
                return;
            }
            if (streaming) {
                uart_put(WANT_STREAM);
            } else {
                uart_put(ACK);
                uart_put(WANT_CRC);
            }
            r = GOT_NONE;
        } else if (blk == 1 && !ymodem) {
            /* XMODEM: its first block's here */
            take_name(xname);
            if (!create()) {
                cancel();
                tell("couldn't make", 0);
                return;
            }
        } else {
            cancel();
            tell("blocks out of step", 0);
            return;
        }

        /* its blocks, to the end */
        expect = 1, errors = 0;
        for (;;) {
            if (r == GOT_NONE) {
                r = block(10 * SECOND);
            }
            if (r == GOT_BLOCK) {
                if (blk == expect) {
                    uint16_t n = len;
                    if (size && got + n > size) {
                        n = (uint16_t)(size - got);
                    }
                    /* ACKed first, so the next comes while this is written */
                    if (!streaming) {
                        uart_put(ACK);
                    }
                    file_write(BLOCK, n);
                    got += n;
                    ++expect, errors = 0;
                    progress(0);
                } else if (blk == (uint8_t)(expect - 1)) {
                    uart_put(ACK); /* again: our ACK was lost */
                } else {
                    cancel();
                    fail("blocks out of step:");
                    return;
                }
            } else if (r == GOT_EOT) {
                uart_put(ACK);
                file_close();
                progress(1);
                tell("received", 1);
                break;
            } else if (r == GOT_CAN) {
                fail("the board stopped:");
                return;
            } else if (r == GOT_STOP) {
                cancel();
                fail("stopped:");
                return;
            } else if (streaming) {
                /* a stream can't be asked again: it's stopped */
                cancel();
                fail("an error in the stream:");
                return;
            } else {
                if (r == GOT_BAD) {
                    purge();
                }
                if (++errors == MOST_ERRORS) {
                    cancel();
                    fail("too many errors:");
                    return;
                }
                /* the first data block of YMODEM's asked for again as it was */
                uart_put(ymodem && expect == 1 ? WANT_CRC : NAK);
            }
            r = GOT_NONE;
        }
        if (!ymodem) {
            return;
        }
    }
}

/* the board told to stop, ZMODEM's way and XMODEM's */
void zcancel(void) {
    uint8_t i;
    for (i = 0; i < 8; ++i) {
        uart_put(CAN);
    }
    for (i = 0; i < 8; ++i) {
        uart_put(0x08);
    }
    cancel();
}

/* ZMODEM's subpacket bank, for the code in BANK_CODE: each picks it, and
 * then the bank that was picked again (see xfer.h) */
#define ZBANK BANK_ZMODEM

void zbank_put(uint16_t at, uint8_t c) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = ZBANK;
    BANKED[at] = c;
    RAM_BANK = bank;
}

uint8_t zbank_get(uint16_t at) {
    uint8_t bank = RAM_BANK, c;
    RAM_BANK = ZBANK;
    c = BANKED[at];
    RAM_BANK = bank;
    return c;
}

void zbank_write(uint16_t n) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = ZBANK;
    file_write(BANKED, n);
    RAM_BANK = bank;
}

uint16_t zbank_read(uint16_t n) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = ZBANK;
    n = file_read(BANKED, n);
    RAM_BANK = bank;
    return n;
}

void zbank_file(uint16_t n) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = ZBANK;
    BANKED[n < 127 ? n : 127] = 0;
    take_name((const char *)BANKED);
    size = header_size(BANKED);
    RAM_BANK = bank;
}

/* ZMODEM's sending: a file of FILES, picked on the status line,
 * as a board's receiver (its ZRINIT) waits for one. Binary headers with a
 * 16-bit CRC, 1K subpackets streamed (ZCRCG), the board's ZRPOS heeded. */

/* the files listed, in their bank: a name and its size each */
#define LIST_BANK BANK_LIST
#define ENTRY 44 /* 40 of name, 4 of size */
#define MOST_FILES (BANK_SIZE / ENTRY)
static uint8_t listed;

/* FILES's files, from its long listing ("$=L": a BASIC program's
 * lines, each its size in K, the name in quotes, its type, its date,
 * and its size in bytes, 8 hex digits, last), into their bank */
static void list_files(void) {
    static char line[48];
    int16_t c;
    uint8_t n, i, quotes, fresh, typed, first = 1, bank;
    char kind[3];
    unsigned long bytes;

    listed = 0;
    if (!file_listing()) {
        return;
    }
    file_byte(), file_byte(); /* its load address */
    for (;;) {
        if (file_byte() < 0 || file_byte() < 0) { /* a line's link */
            break;
        }
        file_byte(), file_byte(); /* its number */
        n = 0, quotes = 0, bytes = 0, fresh = 1, typed = 0;
        while ((c = file_byte()) > 0) {
            /* the last run of hex digits: the size */
            uint8_t d = (uint8_t)c;
            d = d >= '0' && d <= '9' ? d - '0' : d >= 'A' && d <= 'F' ? d - 'A' + 10 : 0xFF;
            if (d != 0xFF) {
                bytes = fresh ? d : bytes << 4 | d;
                fresh = 0;
            } else {
                fresh = 1;
            }
            if (c == '"') {
                ++quotes;
            } else if (quotes == 1 && n < 40) {
                line[n++] = (char)c;
            } else if (quotes == 2 && c != ' ' && typed < 3) {
                /* its type, the word after the name: PRG, SEQ, DIR */
                kind[typed++] = (char)c;
            }
        }
        if (c < 0) {
            break;
        }
        if (first) {
            first = 0; /* the card's own name */
            continue;
        }
        /* a folder (DIR) isn't sent; nor is a file with no name */
        if (quotes != 2 || !n || listed == MOST_FILES ||
            (typed == 3 && kind[0] == 'D' && kind[1] == 'I' && kind[2] == 'R')) {
            continue;
        }
        line[n] = 0;
        bank = RAM_BANK;
        RAM_BANK = LIST_BANK;
        for (i = 0; i <= n; ++i) {
            BANKED[listed * ENTRY + i] = (uint8_t)line[i];
        }
        *(unsigned long *)(BANKED + listed * ENTRY + 40) = bytes;
        RAM_BANK = bank;
        ++listed;
    }
    file_close();
}

/* file |i| of those listed, as |name| and |size| (NEAR: the picker's
 * window, in BANK_CODE, calls it) */
NEAR static void listed_file(uint8_t i) {
    uint8_t j, bank = RAM_BANK;
    RAM_BANK = LIST_BANK;
    for (j = 0; j < 40 && (name[j] = (char)BANKED[i * ENTRY + j]); ++j) {
    }
    name[j] = 0;
    size = *(unsigned long *)(BANKED + i * ENTRY + 40);
    RAM_BANK = bank;
}

/* a file picked on the status line: its number, or -1 for none */
/* A file picked in a window of FILES's files, as many as fit, Up and
 * Down going through them: its number, or -1 for none. On a PETSCII
 * screen, or with no room for a window, on the status line instead. */
#define PW_ROW 5
#define PW_COL 14
#define PW_ROWS 12
#define PW_WIDTH 52
static const char *const pick_hints[] = {"Return", " send   ", "Esc", " none", 0};

#define PICKED_NONE 0xFE
#define NO_WINDOW 0xFF

/* the window's part, in BANK_CODE: the file's number, PICKED_NONE, or
 * NO_WINDOW */
IN_BANK static uint8_t pick_window(void) {
    static char line[24];
    window_t w;
    uint8_t i = 0, top = 0, k;
    if (ui_open(&w, PW_ROW, PW_COL, PW_ROWS + 5, PW_WIDTH, "Send which file?")) {
        ui_hints(PW_ROW + PW_ROWS + 3, PW_COL + 3, pick_hints);
        for (;;) {
            uint8_t r;
            if (i < top) {
                top = i;
            } else if (i >= top + PW_ROWS) {
                top = i - PW_ROWS + 1;
            }
            for (r = 0; r < PW_ROWS; ++r) {
                uint8_t j = top + r, at = PW_ROW + 2 + r;
                uint8_t attr = j == i ? UI_PICKED : UI_TEXT;
                ui_fill(at, PW_COL + 2, PW_WIDTH - 4, ' ', j < listed ? attr : UI_TEXT);
                if (j < listed) {
                    char *p;
                    listed_file(j);
                    ui_text(at, PW_COL + 3, name, attr, 0);
                    p = put_number(line, size);
                    p = put_text(p, " bytes");
                    *p = 0;
                    ui_text(at, PW_COL + PW_WIDTH - 3 - (uint8_t)(p - line), line, attr, 0);
                }
            }
            do {
                k = keys_get();
            } while (!k);
            if (k == KEY_UP) {
                i = i ? i - 1 : listed - 1;
            } else if (k == KEY_DOWN) {
                i = i + 1 < listed ? i + 1 : 0;
            } else if (k == KEY_RETURN || k == KEY_ESC) {
                ui_close(&w);
                return k == KEY_RETURN ? i : PICKED_NONE;
            }
        }
    }
    return NO_WINDOW;
}

static int16_t pick(void) {
    static char line[96];
    uint8_t i = 0, k;
    if (!screen_petscii && (i = far8(pick_window, NO_WINDOW)) != NO_WINDOW) {
        return i == PICKED_NONE ? -1 : i;
    }
    i = 0;
    for (;;) {
        char *p = line;
        listed_file(i);
        p = put_text(p, screen_cols == 40 ? " send " : " Send which file?  ");
        p = put_text(p, name);
        *p++ = ' ', *p++ = ' ';
        p = put_number(p, size);
        if (screen_cols != 40) {
            p = put_text(p, " bytes  (");
            p = put_number(p, i + 1);
            p = put_text(p, " of ");
            p = put_number(p, listed);
            p = put_text(p, ")  Up/Down  Return  Esc");
        }
        *p = 0;
        screen_status(line);
        do {
            k = keys_get();
        } while (!k);
        if (k == KEY_UP || k == KEY_LEFT) {
            i = i ? i - 1 : listed - 1;
        } else if (k == KEY_DOWN || k == KEY_RIGHT) {
            i = i + 1 < listed ? i + 1 : 0;
        } else if (k == KEY_RETURN) {
            return i;
        } else if (k == KEY_ESC) {
            return -1;
        }
    }
}

/* the file opened to read, at |pos| */
uint8_t open_at(unsigned long pos) {
    char *p = put_text(spec, name);
    put_text(p, ",S,R")[0] = 0;
    file_close();
    if (!file_open(spec)) {
        return 0;
    }
    while (pos) {
        uint16_t n = zbank_read(pos > 1024 ? 1024 : (uint16_t)pos);
        if (!n) {
            return 0;
        }
        pos -= n;
    }
    /* the keyboard the input again: GETIN reads whatever the input is, and
     * file_read picks the file each time */
    cbm_k_clrch();
    return 1;
}

/* ZMODEM's code: in BANK_CODE, loaded from BUSTERM.BNK as busterm starts,
 * if it's the one made with this busterm (its stamp, this build's) */
#define BANK_FILE "BUSTERM.BNK"
static const char stamp[] = "busterm " BUILD_ID;
static uint8_t bank_ready;

void transfer_init(void) {
    uint8_t i, bank = RAM_BANK;
    file_load(BANK_FILE, BANK_CODE, BANKED);
    RAM_BANK = BANK_CODE;
    for (i = 0; i < sizeof stamp && BANKED[i] == (uint8_t)stamp[i]; ++i) {
    }
    RAM_BANK = bank;
    bank_ready = i == sizeof stamp;
}

uint8_t transfer_ready(void) { return bank_ready; }

/* ZMODEM without its code: the board told to stop, and why said; 1 */
static uint8_t no_bank(void) {
    if (bank_ready) {
        return 0;
    }
    zcancel();
    tell("ZMODEM needs " BANK_FILE ", beside busterm", 0);
    return 1;
}

/* |code|, in BANK_CODE, run */
static void far(void (*code)(void)) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = BANK_CODE;
    code();
    RAM_BANK = bank;
}

/* and its answer; or, without BUSTERM.BNK, |without| */
static uint8_t far8(uint8_t (*code)(void), uint8_t without) {
    uint8_t bank = RAM_BANK, r;
    if (!bank_ready) {
        return without;
    }
    RAM_BANK = BANK_CODE;
    r = code();
    RAM_BANK = bank;
    return r;
}

/* a file listed, picked and sent; or none, the board told so */
static void upload(const char *unused) {
    int16_t i;
    (void)unused;
    if (no_bank()) {
        return;
    }
    list_files();
    if (!listed) {
        zcancel();
        tell("nothing in FILES to send", 0);
        return;
    }
    i = pick();
    if (i < 0) {
        zcancel();
        tell("nothing sent", 0);
        return;
    }
    listed_file((uint8_t)i);
    far(zsend);
}

/* Sending XMODEM's way or YMODEM's: a file of FILES, picked on the status
 * line, to a board's receiver that asks for it: 'C' for a CRC, NAK for
 * the checksum, or 'G' for YMODEM-g's stream, sent with no ACKs waited
 * for. YMODEM sends block 0 first, the name and size, then its file in 1K
 * blocks; XMODEM in 128s. The last is filled out with ^Z. */

#define PAD 0x1A
#define ASK_TIME (60 * SECOND) /* for the board's receiver to start */

/* what came, as a block's sent */
enum { SENT, NOT_SENT, SEND_STOPPED, SEND_CANCELLED };

/* the board's receiver's ask: 'C', NAK or 'G'; or TIMEOUT, STOPPED, or CAN
 * (two of them) */
static uint16_t asked(uint16_t wait) {
    for (;;) {
        uint16_t c = get(wait);
        if (c == WANT_CRC || c == NAK || c == WANT_STREAM || c > 0xFF) {
            return c;
        }
        if (c == CAN && get(SECOND) == CAN) {
            return CAN;
        }
    }
}

/* The receiver's answer to a block or an EOT: ACK, NAK, CAN (two), or
 * TIMEOUT or STOPPED. Anything else it sends is passed over, as lrzsz's
 * sender does: on a board, what rz says on its terminal (its progress,
 * say) comes to the caller too, mixed in with its answers. */
static uint16_t answer(void) {
    for (;;) {
        uint16_t c = get(10 * SECOND);
        if (c == ACK || c == NAK || c > 0xFF) {
            return c;
        }
        if (c == CAN && get(SECOND) == CAN) {
            return CAN;
        }
    }
}

/* Block |num| (its |n| bytes at BLOCK, filled out to |of| with |pad|)
 * sent till it's ACKed, or once, streaming */
static uint8_t send_block(uint8_t num, uint16_t n, uint16_t of, uint8_t pad) {
    uint8_t tries;
    while (n < of) {
        BLOCK[n++] = pad;
    }
    for (tries = 0; tries < MOST_ERRORS; ++tries) {
        uint8_t hi = 0, lo = 0, sum = 0;
        uint16_t i, c;
        uart_put(of == 1024 ? STX : SOH);
        uart_put(num);
        uart_put((uint8_t)~num);
        for (i = 0; i < of; ++i) {
            uint8_t b = BLOCK[i], idx = hi ^ b;
            uart_put(b);
            sum += b;
            hi = lo ^ CRC_HI[idx];
            lo = CRC_LO[idx];
        }
        if (use_crc) {
            uart_put(hi);
            uart_put(lo);
        } else {
            uart_put(sum);
        }
        if (streaming) {
            /* nothing waited for: only a CAN, or Esc */
            if (uart_ready() && uart_get() == CAN) {
                return SEND_CANCELLED;
            }
            return keys_get() == KEY_ESC ? SEND_STOPPED : SENT;
        }
        c = answer();
        if (c == ACK) {
            return SENT;
        }
        if (c == STOPPED) {
            return SEND_STOPPED;
        }
        if (c == CAN) {
            return SEND_CANCELLED;
        }
        /* a NAK, or nothing: again */
    }
    return NOT_SENT;
}

/* As the board's receiver asked: YMODEM (or XMODEM), and its first ask,
 * seen already, as busterm watched for it */
static uint8_t send_ymodem, first_ask;

/* the board's receiver asked, for a file (its first ask, if it's come) or
 * (|first| not) the next: 1, or 0, why said */
static uint8_t start(uint8_t first) {
    uint16_t c = first && first_ask ? first_ask : asked(first ? ASK_TIME : 10 * SECOND);
    first_ask = 0;
    if (c == STOPPED) {
        cancel();
        tell("stopped sending", 0);
    } else if (c == CAN) {
        tell("the board stopped", 0);
    } else if (c > 0xFF) {
        cancel();
        tell("the board didn't ask for", 0);
    } else {
        use_crc = c != NAK;
        streaming = c == WANT_STREAM;
        return 1;
    }
    return 0;
}

/* a block's not being sent, said */
static void unsent(uint8_t r) {
    if (r != SEND_CANCELLED) {
        cancel();
    }
    file_close();
    tell(r == SEND_STOPPED     ? "stopped sending"
         : r == SEND_CANCELLED ? "the board stopped"
                               : "too many errors sending",
         1);
}

/* a file listed, picked, and sent as YMODEM or XMODEM, as the board asked */
static void send(const char *unused) {
    int16_t i;
    uint8_t r, num;
    uint16_t n;
    (void)unused;
    list_files();
    if (!listed || (i = pick()) < 0) {
        cancel();
        tell(listed ? "nothing sent" : "nothing in FILES to send", 0);
        return;
    }
    listed_file((uint8_t)i);
    ymodem = send_ymodem, sending = 1, got = 0, one_k = ymodem;
    progress(1);
    if (!open_at(0)) {
        cancel();
        tell("couldn't read", 0);
        return;
    }
    if (!start(1)) {
        file_close();
        return;
    }
    if (ymodem) {
        /* block 0: the name and size */
        char *p = put_text((char *)BLOCK, name);
        *p++ = 0;
        p = put_number(p, size);
        *p++ = 0;
        r = send_block(0, (uint16_t)((uint8_t *)p - BLOCK), 128, 0);
        if (r != SENT) {
            unsent(r);
            return;
        }
        if (!start(0)) {
            file_close();
            return;
        }
    }
    for (num = 1;; ++num) {
        uint16_t want = ymodem ? 1024 : 128;
        n = file_read(BLOCK, want);
        if (!n) {
            break;
        }
        r = send_block(num, n, ymodem && n > 128 ? 1024 : 128, PAD);
        if (r != SENT) {
            unsent(r);
            return;
        }
        got += n;
        progress(0);
        if (n < want) {
            break;
        }
    }
    file_close();
    /* The end: EOT till it's ACKed (YMODEM's first is NAKed). Every block
     * was ACKed, so the board going on to its next screen instead is the
     * receiver done too: a board can lose rz's last ACK as rz exits, and
     * what's sent it after would go to its menu. */
    for (r = 0;; ++r) {
        uint16_t c;
        if (r == MOST_ERRORS) {
            cancel();
            tell("the board didn't take the end of", 1);
            return;
        }
        uart_put(EOT);
        c = get(10 * SECOND);
        if (c == ACK || (c < 0x100 && c != NAK && c != CAN)) {
            break;
        }
        if (c == STOPPED) {
            cancel();
            tell("stopped sending", 1);
            return;
        }
        if (c == CAN && get(SECOND) == CAN) {
            tell("the board stopped", 1);
            return;
        }
    }
    progress(1);
    tell("sent", 1);
    if (ymodem && start(0)) {
        /* the batch's end: a block 0 with no name */
        send_block(0, 0, 128, 0);
    }
}

/* |receiver| run in the FILES folder, made if it isn't there */
static void in_downloads(void (*receiver)(const char *), const char *xname,
                         void (*said)(const char *)) {
    uint8_t bank = RAM_BANK;
    say = said;
    name[0] = 0, zmodem = 0, sending = 0;
    if (!uart_carrier()) {
        tell("no call to receive from", 0);
        return;
    }
    RAM_BANK = BANK_TRANSFER;
    tables();
    file_command("MD:FILES");
    if (file_command("CD:FILES")) {
        receiver(xname);
        window_shut();
        file_command("CD:..");
    } else {
        cancel();
        tell("couldn't open the FILES folder", 0);
    }
    RAM_BANK = bank;
}

static void zreceive_named(const char *unused) {
    (void)unused;
    if (!no_bank()) {
        far(zreceive);
    }
}

void transfer_zmodem(void (*said)(const char *)) { in_downloads(zreceive_named, 0, said); }

void transfer_receive(const char *xname, void (*said)(const char *)) {
    in_downloads(receive, xname, said);
}

void transfer_upload(void (*said)(const char *)) { in_downloads(upload, 0, said); }

void transfer_send(void (*said)(const char *), uint8_t ymodem, uint8_t ask) {
    send_ymodem = ymodem, first_ask = ask;
    in_downloads(send, 0, said);
}
