#include "transfer.h"

#include <cx16.h>

#include "banks.h"
#include "files.h"
#include "keys.h"
#include "screen.h"
#include "uart.h"

#define SOH 0x01 /* a block of 128 */
#define STX 0x02 /* a block of 1024 */
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define WANT_CRC 'C'
#define WANT_STREAM 'G' /* YMODEM-g: blocks one after another, no ACKs */

#define SECOND 60      /* jiffies */
#define START_TRIES 10 /* 'C' (or NAK) every 3 seconds, while nothing comes */
#define CRC_TRIES 4    /* then XMODEM's checksum asked for instead */
#define MOST_ERRORS 10

/* in banked RAM: the block, and the CRC's table in two halves */
#define BLOCK BANKED
#define CRC_HI (BANKED + 0x400)
#define CRC_LO (BANKED + 0x500)

/* what get() gives besides a byte */
#define TIMEOUT 0x100
#define STOPPED 0x200 /* Esc, or the call's gone */

/* what came, as a block's looked for */
enum { GOT_BLOCK, GOT_BAD, GOT_NOTHING, GOT_EOT, GOT_CAN, GOT_STOP, GOT_NONE };

static void (*say)(const char *);
static uint8_t use_crc, ymodem, streaming;
static uint8_t blk;   /* the block's number */
static uint16_t len;  /* and length */
static char name[41]; /* the file's */
static char spec[52]; /* and what the drive's told */
static uint8_t one_k;
static unsigned long size, got; /* size: 0 if not known (XMODEM) */
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
static uint16_t get(uint16_t wait) {
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
static uint8_t uart_get_within(uint16_t wait) {
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

static char *put_text(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

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

/* the status line: the file and how far it's got, at most 4 times a
 * second unless |now| */
static void progress(uint8_t now) {
    static char line[81];
    char *p = line;
    unsigned long t = cx16_k_rdtim();
    if (!now && t - shown_at < SECOND / 4) {
        return;
    }
    shown_at = t;
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
        p = put_text(p, " receiving ");
        p = put_text(p, name[0] ? name : "...");
        *p++ = ' ', *p++ = ' ';
        p = put_number(p, got);
        if (size) {
            p = put_text(p, " of ");
            p = put_number(p, size);
        }
        p = put_text(p, ymodem ? " bytes  YMODEM" : " bytes  XMODEM");
        p = put_text(p, streaming ? "-g" : one_k ? "-1K" : use_crc ? "-CRC" : "");
        p = put_text(p, "   Esc stops");
    }
    *p = 0;
    screen_status(line);
}

/* The board told to stop, and what it had sent already thrown away, till
 * a second's quiet (or 20 seconds, if it won't stop), so the terminal
 * isn't filled with it. */
static void cancel(void) {
    unsigned long until = cx16_k_rdtim() + 20 * SECOND;
    uint8_t i;
    do {
        for (i = 0; i < 8; ++i) {
            uart_put(CAN);
        }
        for (i = 0; i < 255 && uart_get_within(SECOND); ++i) {
        }
    } while (i == 255 && cx16_k_rdtim() < until);
}

/* |s| and the file's name, said */
static void tell(const char *s, uint8_t with_bytes) {
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
    say(line);
}

/* the file being written left half done: told why, and gone */
static void fail(const char *why) {
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
static uint8_t create(void) {
    char *p = put_text(spec, "@:");
    p = put_text(p, name);
    put_text(p, ",S,W")[0] = 0;
    got = 0;
    return file_create(spec);
}

/* the size in YMODEM's header, after the name */
static unsigned long header_size(void) {
    const uint8_t *p = BLOCK;
    unsigned long n = 0;
    while (*p && p < BLOCK + 127) {
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
            size = header_size();
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

void transfer_receive(const char *xname, void (*said)(const char *)) {
    uint8_t bank = RAM_BANK;
    say = said;
    name[0] = 0;
    if (!uart_carrier()) {
        tell("no call to receive from", 0);
        return;
    }
    RAM_BANK = BANK_TRANSFER;
    tables();
    file_command("MD:DOWNLOADS");
    if (file_command("CD:DOWNLOADS")) {
        receive(xname);
        file_command("CD:..");
    } else {
        cancel();
        tell("couldn't open the DOWNLOADS folder", 0);
    }
    RAM_BANK = bank;
}
