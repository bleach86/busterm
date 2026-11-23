/* busterm: a BBS terminal for the Commander X16 and TexElec's Serial &
 * ESP32 Network Card. Copyright (c) 2026 bleach86. MIT licence: see
 * LICENSE. */

#include <cx16.h>
#include <stdint.h>

#include "ansi.h"
#include "banks.h"
#include "capture.h"
#include "dir.h"
#include "keys.h"
#include "petscii.h"
#include "screen.h"
#include "scrollback.h"
#include "settings.h"
#include "sound.h"
#include "transfer.h"
#include "uart.h"
#include "ui.h"

#define VERSION "0.1.2"

/* at most this many bytes drawn before the keyboard is looked at */
#define BURST 255

#define JIFFIES 60UL

static uint8_t online;
static uint8_t help;
static unsigned long online_since; /* jiffies */
static unsigned long next_tick;    /* the status line's next redraw */

/* a blinking cursor: shown or not, and when it next changes */
#define BLINK_JIFFIES 20
static uint8_t blink_on;
static unsigned long next_blink;

/* the board called last: its name for the status line, and whether the
 * call's still to connect (and be counted) */
static char board[NAME_LEN + 1];
static char board_addr[ADDR_LEN + 1];
static uint8_t calling;

/* the status line being made: 80 columns, and room to spare */
static char line[96];

static char *put_text(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

/* |s|, then spaces to |width| */
static char *put_padded(char *p, const char *s, uint8_t width) {
    while (*s && width) {
        *p++ = *s++, --width;
    }
    while (width--) {
        *p++ = ' ';
    }
    return p;
}

static char *put_2digits(char *p, uint8_t n) {
    *p++ = '0' + n / 10;
    *p++ = '0' + n % 10;
    return p;
}

static char *put_online(char *p) {
    if (online) {
        unsigned long secs = (cx16_k_rdtim() - online_since) / JIFFIES;
        p = put_text(p, "online ");
        p = put_2digits(p, (uint8_t)(secs / 3600 % 100));
        *p++ = ':';
        p = put_2digits(p, (uint8_t)(secs / 60 % 60));
        *p++ = ':';
        p = put_2digits(p, (uint8_t)(secs % 60));
        return p;
    }
    return put_text(p, calling ? "calling...     " : "offline        ");
}

/* the status line: busterm, the board, the emulation and speed, online
 * and for how long, then the clock and where help is at the right; on a
 * 40 column screen, the board, online and the clock */
static void status(void) {
    char *p = line;
    cx16_date_time_t now;

    if (screen_cols == 40) {
        if (help) {
            screen_status(" Alt+ B back D dir H hang R recv X quit");
            return;
        }
        *p++ = capturing ? '*' : ' '; /* capturing: a star */
        p = put_padded(p, online || calling ? board : "busterm", 16);
        *p++ = ' ';
        p = put_online(p);
    } else {
        if (help) {
            screen_status(
                " Alt+ B back C capture D dir E type H hang up R receive S settings X quit"
                " Z back");
            return;
        }
        p = put_text(p, " busterm \x10 ");
        p = put_padded(p, online || calling ? board : "", 20);
        p = put_text(p, screen_petscii ? " PETSCII 80  " : " ANSI 115200 ");
        p = put_online(p);
        /* 59 columns so far: the clock at 61, help at 68 */
    }
    cx16_k_clock_get_date_time(&now);
    if (settings[SET_CLOCK]) {
        /* 12 hours: " 3:24p", in the same 7 columns */
        uint8_t h = now.hour % 12 ? now.hour % 12 : 12;
        *p++ = ' ';
        *p++ = h >= 10 ? '1' : ' ';
        *p++ = '0' + h % 10;
        *p++ = ':';
        p = put_2digits(p, now.min);
        *p++ = now.hour < 12 ? 'a' : 'p';
    } else {
        p = put_text(p, "  ");
        p = put_2digits(p, now.hour);
        *p++ = ':';
        p = put_2digits(p, now.min);
    }
    if (screen_cols != 40) {
        p = put_text(p, capturing ? "  capturing " : "  Alt-Z help");
    }
    *p = 0;
    screen_status(line);
}

/* a byte from the board, to the screen as the board's terminal type */
static void feed(uint8_t c) {
    if (screen_petscii) {
        petscii_feed(c);
    } else {
        ansi_feed(c);
    }
}

/* busterm's own words on the terminal: ANSI's escape codes for colour, or
 * on a Commodore screen the plain text, its letters as PETSCII has them */
static void print(const char *s) {
    if (!screen_petscii) {
        while (*s) {
            ansi_feed((uint8_t)*s++);
        }
        return;
    }
    while (*s) {
        uint8_t c = (uint8_t)*s++;
        if (c == 0x1B) {
            while (*s && !(*s >= 0x40 && *s <= 0x7E && *s != '[')) {
                ++s; /* an escape code: left out */
            }
            if (*s) {
                ++s;
            }
            continue;
        }
        if (c == 0x0A) {
            continue; /* RETURN is the new line */
        }
        if (c >= 'a' && c <= 'z') {
            c -= 0x20;
        } else if (c >= 'A' && c <= 'Z') {
            c += 0x80;
        }
        petscii_feed(c);
    }
}

static void wait_jiffies(uint8_t n) {
    unsigned long until = cx16_k_rdtim() + n;
    while (cx16_k_rdtim() < until) {
    }
}

/* ---- calls ------------------------------------------------------------- */

/* the call was dialled from the directory: back to it when it ends */
static uint8_t from_directory;

/* a PETSCII call has ended: the next key opens the directory (which would
 * clear the screen, the call's last words with it) */
static uint8_t awaiting_key;

/* the modem's answer to a call that didn't connect: NO CARRIER, BUSY, NO
 * ANSWER or ERROR, watched for while calling */
#define WATCHED 10
static char heard[WATCHED];
static uint8_t call_failed;

static uint8_t heard_end(const char *word) {
    uint8_t n = 0, i;
    while (word[n]) {
        ++n;
    }
    for (i = 0; i < n; ++i) {
        if (heard[WATCHED - n + i] != word[i]) {
            return 0;
        }
    }
    return 1;
}

static void watch(uint8_t c) {
    uint8_t i;
    for (i = 0; i + 1 < WATCHED; ++i) {
        heard[i] = heard[i + 1];
    }
    heard[WATCHED - 1] = (char)c;
    if (heard_end("NO CARRIER") || heard_end("BUSY") || heard_end("NO ANSWER") ||
        heard_end("ERROR")) {
        call_failed = 1;
    }
}

/* a byte from the modem */
/* A transfer the board's begun. A download: ZMODEM's ZRQINIT ("**", ZDLE,
 * "B00"), which every ZMODEM sender starts with; or lrzsz's XMODEM, which
 * asks for the receiver in words (YMODEM's sender says nothing: Alt-R). An
 * upload: ZMODEM's receiver's ZRINIT ("B01"); or lrzsz's YMODEM receiver,
 * "rz waiting to receive." and then its 'C' (NAK, or 'G') where ZMODEM's
 * would send its ZRINIT; or its XMODEM receiver, "rz: ready to receive"
 * and the file's name, and on the next line its 'C' or NAK. */
static const char zmodem_start[] = "**\x18"
                                   "B0";
static const char xmodem_start[] = "receive command now.";
static const char ymodem_rx[] = "waiting to receive.";
static const char xmodem_rx[] = "ready to receive ";
static uint8_t z_at, x_at, yr_at, xr_at;
/* 1 ZMODEM, 2 XMODEM, 3 ZMODEM's receiver, 4 XMODEM's, 5 YMODEM's */
static uint8_t begun;
/* a receiver's words seen: 'y' YMODEM's, 'x' XMODEM's (its name's line
 * going on), 'X' XMODEM's (its line ended); and the ask that came after */
static uint8_t rx_said, rx_ask;

/* |c| matched against |s| at |*at|: whether it's matched the whole */
static uint8_t matched(const char *s, uint8_t *at, uint8_t c) {
    if (c == (uint8_t)s[*at]) {
        if (!s[++*at]) {
            *at = 0;
            return 1;
        }
    } else {
        *at = c == (uint8_t)s[0];
    }
    return 0;
}

static void watch_downloads(uint8_t c) {
    if (!zmodem_start[z_at]) {
        /* "B00", a sender's ZRQINIT; "B01", a receiver's ZRINIT */
        z_at = 0;
        begun = c == '0' ? 1 : c == '1' ? 3 : 0;
    } else if (c == (uint8_t)zmodem_start[z_at]) {
        ++z_at;
    } else {
        z_at = c != '*' ? 0 : z_at == 2 ? 2 : 1;
    }
    if (matched(xmodem_start, &x_at, c)) {
        begun = 2;
    }
    if (rx_said && c != '\r' && c != '\n' && rx_said != 'x') {
        /* the first after a receiver's words: its ask, or not it */
        if (c == 'C' || c == 0x15 || (c == 'G' && rx_said == 'y')) {
            begun = rx_said == 'y' ? 5 : 4, rx_ask = c;
        }
        rx_said = 0;
    } else if (rx_said == 'x' && c == '\n') {
        rx_said = 'X';
    }
    if (matched(ymodem_rx, &yr_at, c)) {
        rx_said = 'y';
    }
    if (matched(xmodem_rx, &xr_at, c)) {
        rx_said = 'x';
    }
}

static void incoming(uint8_t c) {
    if (calling && !online) {
        watch(c);
    }
    if (online) {
        watch_downloads(c);
    }
    if (capturing) {
        capture_byte(c);
    }
    feed(c);
}

/* what comes in drawn, for |n| jiffies */
static void drain(uint8_t n) {
    unsigned long until = cx16_k_rdtim() + n;
    while (cx16_k_rdtim() < until) {
        while (uart_ready()) {
            incoming(uart_get());
        }
    }
}

/* Zimodem's way: a second of nothing, +++, a second of nothing */
static void hang_up(void) {
    screen_status(" hanging up...");
    wait_jiffies(70);
    uart_puts("+++");
    wait_jiffies(70);
    drain(20);
    status();
}

/* hung up, and the carrier gone (3 seconds at most), before another call */
static void hang_up_now(void) {
    uint8_t n;
    hang_up();
    for (n = 0; n < 30 && uart_carrier(); ++n) {
        drain(6);
    }
    online = 0, calling = 0, call_failed = 0;
    status();
}

static void directory(void);

/* A call over, or one that never connected: back to the directory, if
 * that's where it was dialled from. Its last words are drawn first, and
 * Esc closes the directory to read them. */
static void call_ended(void) {
    status();
    if (!from_directory) {
        return;
    }
    from_directory = 0;
    drain(60);
    if (screen_petscii) {
        awaiting_key = 1;
        screen_status(" call over: a key for the dir, Esc stays");
    } else {
        directory();
    }
}

static void send_key(uint8_t k) {
    if (!online && k == KEY_DEL) {
        uart_put(0x08); /* the modem's backspace, PETSCII or not */
        return;
    }
    if (screen_petscii) {
        uart_put(k); /* the keyboard types PETSCII: as it is */
        return;
    }
    switch (k) {
    case KEY_RETURN:
        uart_put(0x0D);
        break;
    case KEY_DEL:
        uart_put(settings[SET_BACKSPACE] ? 0x7F : 0x08);
        break;
    case KEY_UP:
        uart_puts("\x1b[A");
        break;
    case KEY_DOWN:
        uart_puts("\x1b[B");
        break;
    case KEY_RIGHT:
        uart_puts("\x1b[C");
        break;
    case KEY_LEFT:
        uart_puts("\x1b[D");
        break;
    case KEY_HOME:
        uart_puts("\x1b[H");
        break;
    default:
        uart_put(k);
        break;
    }
}

static void copy(char *to, const char *from, uint8_t room) {
    while (*from && --room) {
        *to++ = *from++;
    }
    *to = 0;
}

/* what's typed, shown here too (local echo) */
static void echo(uint8_t k) {
    if (screen_petscii) {
        petscii_feed(k);
    } else if (k == KEY_RETURN) {
        ansi_feed(0x0D);
        ansi_feed(0x0A);
    } else if (k == KEY_DEL) {
        ansi_feed(0x08);
        ansi_feed(' ');
        ansi_feed(0x08);
    } else if (k >= 0x20 && k < 0x7F) {
        ansi_feed(k);
    }
}

/* the screen made ANSI's, if it's PETSCII's */
/* The directory closed with a call still on: the screen made PETSCII
 * again, at its width, if that's what the call's was (what was on it's
 * gone, but what the board sends next is drawn as it should be). */
static void back_to_call(uint8_t petscii, uint8_t cols) {
    if (petscii && (online || calling)) {
        screen_petscii_mode(cols);
        petscii_reset();
        status();
    }
}

static void ansi_screen(void) {
    if (screen_petscii) {
        screen_ansi();
        ansi_reset();
        status();
    }
}

/* A board called: the screen made its terminal type's, then ATDT, its
 * address in quotes (the T for telnet). */
static void dial(const board_t *b) {
    copy(board, b->name, sizeof board);
    copy(board_addr, b->addr, sizeof board_addr);
    if (b->emu == EMU_PETSCII) {
        screen_petscii_mode(b->cols);
        petscii_reset();
    } else {
        ansi_screen();
    }
    print("\r\n\x1b[1;36mCalling ");
    print(board);
    print("\x1b[0m (");
    print(board_addr);
    print(")\r\n");
    uart_puts("ATDT\"");
    uart_puts(board_addr);
    uart_puts("\"\r");
    calling = 1, call_failed = 0;
    status();
}

/* "Hang up |name|" and "and call |next|?", for asking */
static char ask1[NAME_LEN + 12], ask2[NAME_LEN + 12];

/* the dialling directory, on ANSI's 80 columns, and a call if one's
 * picked: with a call up, it's hung up first, if that's what's wanted */
static void directory(void) {
    const board_t *b;
    /* the call's screen, PETSCII's, to go back to: the directory's ANSI */
    uint8_t petscii = screen_petscii, cols = screen_cols;
    awaiting_key = 0;
    ansi_screen();
    b = dir_window();
    if (!b) {
        back_to_call(petscii, cols);
        return;
    }
    if (online || calling) {
        char *p = put_text(ask1, "Hang up ");
        p = put_text(p, board);
        *p = 0;
        p = put_text(ask2, "and call ");
        p = put_text(p, b->name);
        *p++ = '?';
        *p = 0;
        if (!ui_ask(ask1, ask2)) {
            back_to_call(petscii, cols);
            return;
        }
        hang_up_now();
    }
    dial(b);
    from_directory = 1;
}

/* The terminal's type, the next one: ANSI, PETSCII 40 columns, PETSCII
 * 80 columns, ANSI again. The screen is cleared, its mode changed. */
static void next_emulation(void) {
    if (!screen_petscii) {
        screen_petscii_mode(40);
        petscii_reset();
        print("[PETSCII, 40 columns]\r\n");
    } else if (screen_cols == 40) {
        screen_petscii_mode(80);
        petscii_reset();
        print("[PETSCII, 80 columns]\r\n");
    } else {
        screen_ansi();
        ansi_reset();
        print("[ANSI]\r\n");
    }
    status();
}

/* a line about a transfer, in the terminal */
static void say(const char *s) {
    print("\r\n\x1b[1;36m");
    print(s);
    print("\x1b[0m\r\n");
}

/* Files from the board, once it's sending: ZMODEM's, or XMODEM's (named
 * DOWNLOAD.BIN) or YMODEM's (named as the board has them) */
static void receive(uint8_t how) {
    bell_stop();
    screen_cursor_hide();
    if (!screen_petscii) {
        print("\r\x1b[K"); /* ZMODEM's start, drawn as it came, gone */
    }
    if (how == 1) {
        transfer_zmodem(say);
    } else if (how >= 3) {
        capture_flush(); /* the capture, if it's picked, sent whole */
        if (how == 3) {
            transfer_upload(say);
        } else {
            transfer_send(say, how == 5, rx_ask);
        }
    } else {
        transfer_receive("DOWNLOAD.BIN", say);
    }
    status();
}

/* a line typed on the status line, for any screen, into |buf| (room for
 * |size| - 1), what's there typed over or added to: 1 for Return, 0 for
 * Esc */
static uint8_t status_input(const char *prompt, char *buf, uint8_t size) {
    bell_stop();
    uint8_t fresh = 1;
    for (;;) {
        uint8_t k, n = 0;
        char *p = put_text(line, prompt);
        p = put_text(p, buf);
        *p++ = '_';
        *p = 0;
        screen_status(line);
        do {
            k = keys_get();
        } while (!k);
        while (buf[n]) {
            ++n;
        }
        if (k == KEY_RETURN) {
            return 1;
        } else if (k == KEY_ESC) {
            return 0;
        } else if (k == KEY_DEL) {
            if (n) {
                buf[n - 1] = 0;
            }
            fresh = 0;
            continue;
        }
        if (screen_petscii) {
            /* PETSCII's letters as they look: unshifted lower case */
            if (k >= 0x41 && k <= 0x5A) {
                k += 0x20;
            } else if (k >= 0xC1 && k <= 0xDA) {
                k -= 0x80;
            }
        }
        if (k > ' ' && k < 0x7F) {
            if (fresh) {
                n = 0; /* a new name, over the old */
                fresh = 0;
            }
            if (n < size - 1) {
                buf[n] = (char)k;
                buf[n + 1] = 0;
            }
        }
    }
}

/* the capture's file name, kept for the next */
static char cname[25] = "CAPTURE.TXT";

static char *put_number(char *p, unsigned long n) {
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

/* Capture stopped, and said how much went to the file */
static void capture_end(void) {
    uint8_t ok;
    unsigned long n = capture_stop(&ok);
    char *p = put_text(line, ok ? "[captured " : "[the capture stopped: the card said no, after ");
    p = put_number(p, n);
    p = put_text(p, " bytes to ");
    p = put_text(p, capture_name());
    *p++ = ']';
    *p = 0;
    say(line);
}

/* Capture on, its file asked for; or off */
static void capture(void) {
    screen_cursor_hide();
    if (capturing) {
        capture_end();
    } else if (status_input(screen_cols == 40 ? " capture to: "
                                              : " Capture what the board sends to: ",
                            cname, sizeof cname)) {
        if (capture_start(cname)) {
            char *p = put_text(line, "[capturing to FILES/");
            p = put_text(p, cname);
            p = put_text(p, "; Alt-C stops]");
            *p = 0;
        } else {
            put_text(put_text(line, "[couldn't capture to "), cname)[0] = 0;
        }
        say(line);
    }
    status();
}

/* a yes or no on the status line, for any screen */
static uint8_t status_ask(const char *question) {
    uint8_t k;
    screen_status(question);
    do {
        k = keys_get();
    } while (k != 'y' && k != 'Y' && k != 'n' && k != 'N' && k != KEY_ESC);
    status();
    return k == 'y' || k == 'Y';
}

/* Alt and a key: busterm's commands. 1 to quit. */
static uint8_t command(uint8_t c) {
    switch (c) {
    case 'z':
        help = !help;
        status();
        break;
    case 'b':
    case KEY_PGUP:
        scrollback_view(KEY_PGUP);
        status();
        break;
    case 'd':
        directory();
        break;
    case 'e':
        next_emulation();
        break;
    case 's':
        ansi_screen();
        settings_window();
        status();
        break;
    case 'r':
        receive(2);
        break;
    case 'c':
        capture();
        break;
    case 'h':
        if (online || calling) {
            hang_up();
        }
        break;
    case 'x':
        if (online) {
            if (!status_ask(" Hang up and quit? Y or N")) {
                break;
            }
            hang_up_now();
        }
        if (capturing) {
            capture_end();
        }
        return 1;
    default:
        break;
    }
    return 0;
}

int main(void) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = BANK_DATA;
    screen_ansi();
    ansi_reset();
    uart_init(UART_DIVISOR_115200);
    keys_init();
    transfer_init();
    dir_load();
    settings_load();

    print("\x1b[1;36mbusterm " VERSION "\x1b[0m  a BBS terminal for the Commander X16\r\n\r\n");
    print("The modem is ready for commands. To call a board:\r\n\r\n");
    print("   \x1b[1;37mATDT\"bbs.example.org:23\"\x1b[0m\r\n\r\n");
    print("Alt-D is the dialling directory, Alt-Z shows busterm's keys.\r\n\r\n");
    if (!transfer_ready()) {
        print("\x1b[1;33mBUSTERM.BNK isn't beside busterm: ZMODEM won't work.\x1b[0m\r\n\r\n");
    }
    online = uart_carrier();
    online_since = cx16_k_rdtim();
    status();
    if (!settings[SET_START]) {
        directory();
    }

    for (;;) {
        uint8_t k;

        ansi_bs_erases = !online;
        if (uart_ready()) {
            uint8_t n = BURST;
            screen_cursor_hide();
            do {
                incoming(uart_get());
            } while (--n && uart_ready() && !begun);
        }
        if (begun) {
            uint8_t how = begun;
            begun = 0;
            receive(how);
        }
        if (call_failed) {
            call_failed = 0;
            calling = 0;
            call_ended();
        }

        k = keys_alt();
        if (k && command(k)) {
            break;
        }
        k = keys_get();
        if (k) {
            if (awaiting_key) {
                /* Esc: the board's last words kept; any other key: the
                 * directory */
                if (k == KEY_ESC) {
                    awaiting_key = 0;
                    status();
                } else {
                    directory();
                }
            } else {
                send_key(k);
                if (online && settings[SET_ECHO]) {
                    screen_cursor_hide();
                    echo(k);
                }
            }
        }
        bell_tick();

        if (!uart_carrier() != !online) {
            online = uart_carrier();
            online_since = cx16_k_rdtim();
            if (online) {
                if (calling) {
                    dir_called(board, board_addr);
                } else {
                    from_directory = 0; /* dialled at the modem's prompt */
                }
                calling = 0;
                status();
            } else {
                calling = 0;
                call_ended();
            }
        }
        if (cx16_k_rdtim() >= next_tick) {
            next_tick = cx16_k_rdtim() + JIFFIES;
            if (!help && !awaiting_key) {
                status();
            }
        }
        if (!uart_ready()) {
            if (!settings[SET_CURSOR]) {
                screen_cursor_show();
            } else {
                if (cx16_k_rdtim() >= next_blink) {
                    next_blink = cx16_k_rdtim() + BLINK_JIFFIES;
                    blink_on = !blink_on;
                }
                if (blink_on) {
                    screen_cursor_show();
                } else {
                    screen_cursor_hide();
                }
            }
        }
    }

    keys_done();
    screen_restore();
    RAM_BANK = bank;
    return 0;
}
