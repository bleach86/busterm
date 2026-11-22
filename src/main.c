/* busterm: a BBS terminal for the Commander X16 and TexElec's Serial &
 * ESP32 Network Card. Copyright (c) 2026 bleach86. MIT licence: see
 * LICENSE. */

#include <cx16.h>
#include <stdint.h>

#include "ansi.h"
#include "banks.h"
#include "dir.h"
#include "keys.h"
#include "screen.h"
#include "uart.h"

#define VERSION "0.1"

/* at most this many bytes drawn before the keyboard is looked at */
#define BURST 255

#define JIFFIES 60UL

static uint8_t online;
static uint8_t help;
static unsigned long online_since; /* jiffies */
static unsigned long next_tick;    /* the status line's next redraw */

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

/* the status line: busterm, the board, the emulation and speed, online
 * and for how long, then the clock and where help is at the right */
static void status(void) {
    char *p = line;
    cx16_date_time_t now;

    if (help) {
        screen_status(" Alt-D directory   Alt-H hang up   Alt-X quit   Alt-Z back"
                      "     ATDT\"host:port\" dials");
        return;
    }
    p = put_text(p, " busterm \x10 ");
    p = put_padded(p, online || calling ? board : "", 20);
    p = put_text(p, " ANSI 115200 ");
    if (online) {
        unsigned long secs = (cx16_k_rdtim() - online_since) / JIFFIES;
        p = put_text(p, "online ");
        p = put_2digits(p, (uint8_t)(secs / 3600 % 100));
        *p++ = ':';
        p = put_2digits(p, (uint8_t)(secs / 60 % 60));
        *p++ = ':';
        p = put_2digits(p, (uint8_t)(secs % 60));
    } else {
        p = put_text(p, calling ? "calling...     " : "offline        ");
    }
    /* 59 columns so far: the clock at 61, help at 68 */
    p = put_text(p, "  ");
    cx16_k_clock_get_date_time(&now);
    p = put_2digits(p, now.hour);
    *p++ = ':';
    p = put_2digits(p, now.min);
    p = put_text(p, "  Alt-Z help");
    *p = 0;
    screen_status(line);
}

static void print(const char *s) {
    while (*s) {
        ansi_feed((uint8_t)*s++);
    }
}

static void wait_jiffies(uint8_t n) {
    unsigned long until = cx16_k_rdtim() + n;
    while (cx16_k_rdtim() < until) {
    }
}

/* Zimodem's way: a second of nothing, +++, a second of nothing */
static void hang_up(void) {
    screen_status(" hanging up...");
    wait_jiffies(70);
    uart_puts("+++");
    wait_jiffies(70);
    status();
}

static void send_key(uint8_t k) {
    switch (k) {
    case KEY_RETURN:
        uart_put(0x0D);
        break;
    case KEY_DEL:
        uart_put(0x08);
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

/* a board called: ATDT, its address in quotes (the T for telnet) */
static void dial(const char *name, const char *addr) {
    copy(board, name, sizeof board);
    copy(board_addr, addr, sizeof board_addr);
    print("\r\n\x1b[1;36mCalling ");
    print(board);
    print("\x1b[0m (");
    print(board_addr);
    print(")\r\n");
    uart_puts("ATDT\"");
    uart_puts(board_addr);
    uart_puts("\"\r");
    calling = 1;
    status();
}

/* the dialling directory, and a call if one's picked */
static void directory(void) {
    const char *name, *addr;
    if (dir_window(&name, &addr)) {
        dial(name, addr);
    }
}

/* Alt and a key: busterm's commands. 1 to quit. */
static uint8_t command(uint8_t c) {
    switch (c) {
    case 'z':
        help = !help;
        status();
        break;
    case 'd':
        directory();
        break;
    case 'h':
        hang_up();
        break;
    case 'x':
        return 1;
    default:
        break;
    }
    return 0;
}

int main(void) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = BANK_WINDOWS;
    screen_init();
    ansi_reset();
    uart_init(UART_DIVISOR_115200);
    keys_init();
    dir_load();

    print("\x1b[1;36mbusterm " VERSION "\x1b[0m  a BBS terminal for the Commander X16\r\n\r\n");
    print("The modem is ready for commands. To call a board:\r\n\r\n");
    print("   \x1b[1;37mATDT\"bbs.example.org:23\"\x1b[0m\r\n\r\n");
    print("Alt-D is the dialling directory, Alt-Z shows busterm's keys.\r\n\r\n");
    online = uart_carrier();
    online_since = cx16_k_rdtim();
    status();
    directory();

    for (;;) {
        uint8_t k;

        if (uart_ready()) {
            uint8_t n = BURST;
            screen_cursor_hide();
            do {
                ansi_feed(uart_get());
            } while (--n && uart_ready());
        }

        k = keys_alt();
        if (k && command(k)) {
            break;
        }
        k = keys_get();
        if (k) {
            send_key(k);
        }

        if (!uart_carrier() != !online) {
            online = uart_carrier();
            online_since = cx16_k_rdtim();
            if (online && calling) {
                dir_called(board, board_addr);
            }
            calling = 0;
            status();
        }
        if (cx16_k_rdtim() >= next_tick) {
            next_tick = cx16_k_rdtim() + JIFFIES;
            if (!help) {
                status();
            }
        }
        if (!uart_ready()) {
            screen_cursor_show();
        }
    }

    keys_done();
    screen_restore();
    RAM_BANK = bank;
    return 0;
}
