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
static uint8_t use_crc, ymodem, streaming, zmodem, sending;
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
        p = put_text(p, sending ? " sending " : " receiving ");
        p = put_text(p, name[0] ? name : "...");
        *p++ = ' ', *p++ = ' ';
        p = put_number(p, got);
        if (size) {
            p = put_text(p, " of ");
            p = put_number(p, size);
        }
        p = put_text(p, zmodem ? " bytes  ZMODEM" : ymodem ? " bytes  YMODEM" : " bytes  XMODEM");
        p = put_text(p, zmodem ? "" : streaming ? "-g" : one_k ? "-1K" : use_crc ? "-CRC" : "");
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

/* ZMODEM's receiving: its headers (hex, or binary with a 16-bit CRC; the
 * 32-bit one isn't asked for, so isn't sent), and its subpackets, streamed
 * with no ACKs, as RTS/CTS holds the board back while a subpacket's
 * written. A bad one asks for the file again from where it went wrong
 * (ZRPOS). */

#define ZPAD '*'
#define ZDLE 0x18
#define ZBIN 'A'
#define ZHEX 'B'
#define XON 0x11

/* the frames it knows */
#define ZRQINIT 0
#define ZRINIT 1
#define ZSINIT 2
#define ZACK 3
#define ZFILE 4
#define ZNAK 6
#define ZSKIP 5
#define ZFIN 8
#define ZRPOS 9
#define ZDATA 10
#define ZEOF 11

/* a subpacket's ends: the frame's end; more coming; an ACK wanted, more
 * coming; an ACK wanted, the frame's end */
#define ZCRCE 'h'
#define ZCRCG 'i'
#define ZCRCQ 'j'
#define ZCRCW 'k'
#define ZRUB0 'l' /* $7F */
#define ZRUB1 'm' /* $FF */

/* ZFILE's ZF0: binary, as it is */
#define ZCBIN 1

/* ZRINIT's flags: both ways at once, and receiving while writing */
#define CANFDX 0x01
#define CANOVIO 0x02

/* what's read, besides a byte (and TIMEOUT, STOPPED) */
#define FRAME_END 0x1000 /* with the end's letter */
#define Z_BAD 0x300
#define Z_CANCEL 0x400

/* a subpacket, in its own bank: up to 8K */
#define ZBANK BANK_ZMODEM
#define ZMOST 8192

static uint8_t hdr[5]; /* a header's type and its 4 bytes */
static uint16_t zlen;  /* a subpacket's length */

/* the CRC kept a byte on (the table's bank picked) */
static uint8_t crc_hi, crc_lo;
static void crc(uint8_t c) {
    uint8_t idx = crc_hi ^ c;
    crc_hi = crc_lo ^ CRC_HI[idx];
    crc_lo = CRC_LO[idx];
}

static void put_hex(uint8_t b) {
    static const char digits[] = "0123456789abcdef";
    uart_put(digits[b >> 4]);
    uart_put(digits[b & 15]);
}

/* a hex header: |type| and its 4 bytes as hdr[1] to hdr[4] */
static void send_header(uint8_t type) {
    uint8_t i;
    hdr[0] = type;
    uart_put(ZPAD), uart_put(ZPAD), uart_put(ZDLE), uart_put(ZHEX);
    crc_hi = crc_lo = 0;
    for (i = 0; i < 5; ++i) {
        put_hex(hdr[i]);
        crc(hdr[i]);
    }
    put_hex(crc_hi), put_hex(crc_lo);
    uart_put('\r'), uart_put(0x8A);
    if (type != ZFIN && type != ZACK) {
        uart_put(XON);
    }
}

/* a header with a file position */
static void send_pos(uint8_t type, unsigned long pos) {
    hdr[1] = (uint8_t)pos, hdr[2] = (uint8_t)(pos >> 8);
    hdr[3] = (uint8_t)(pos >> 16), hdr[4] = (uint8_t)(pos >> 24);
    send_header(type);
}

static void send_rinit(void) {
    hdr[1] = hdr[2] = hdr[3] = 0; /* no buffer's limit: stream */
    hdr[4] = CANFDX | CANOVIO;
    send_header(ZRINIT);
}

static unsigned long header_pos(void) {
    return hdr[1] | (uint16_t)hdr[2] << 8 | (unsigned long)hdr[3] << 16 |
           (unsigned long)hdr[4] << 24;
}

/* a byte, XON and XOFF passed over */
static uint16_t zraw(void) {
    for (;;) {
        uint16_t c = uart_ready() ? uart_get() : get(10 * SECOND);
        if ((c & 0x7F) != 0x11 && (c & 0x7F) != 0x13) {
            return c;
        }
    }
}

/* a byte of a binary header or subpacket, ZDLE's escapes undone; or a
 * subpacket's end (FRAME_END and its letter), TIMEOUT, STOPPED, Z_BAD or
 * Z_CANCEL (five CANs) */
static uint16_t zget(void) {
    uint16_t c = zraw();
    uint8_t cans = 1;
    if (c != ZDLE) {
        return c;
    }
    while ((c = zraw()) == ZDLE) {
        if (++cans == 5) {
            return Z_CANCEL;
        }
    }
    if (c > 0xFF) {
        return c;
    }
    if (c >= ZCRCE && c <= ZCRCW) {
        return FRAME_END | c;
    }
    if (c == ZRUB0) {
        return 0x7F;
    }
    if (c == ZRUB1) {
        return 0xFF;
    }
    return (c & 0x60) == 0x40 ? c ^ 0x40 : Z_BAD;
}

static uint16_t hex_digit(uint16_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c > 0xFF ? c : Z_BAD;
}

/* a hex header's byte */
static uint16_t hex_byte(void) {
    uint16_t hi = hex_digit(zraw()), lo;
    if (hi > 0xFF) {
        return hi;
    }
    lo = hex_digit(zraw());
    return lo > 0xFF ? lo : hi << 4 | lo;
}

/* The next header, what's before it passed over: its type, hdr[] filled;
 * or TIMEOUT, STOPPED, Z_BAD (its CRC wrong, or one asked not to be
 * sent) or Z_CANCEL. |pads|: its ZPAD read already. */
static uint16_t zheader_from(uint8_t pads) {
    uint16_t c;
    uint8_t i, cans = 0, hex;
    for (;;) {
        c = zraw();
        if (c > 0xFF) {
            return c;
        }
        if (c == ZPAD) {
            pads = 1, cans = 0;
            continue;
        }
        if (c == ZDLE && pads) {
            break;
        }
        pads = 0;
        if (c == CAN && ++cans == 5) {
            return Z_CANCEL;
        } else if (c != CAN) {
            cans = 0;
        }
    }
    c = zraw();
    if (c != ZBIN && c != ZHEX) {
        return c > 0xFF ? c : Z_BAD;
    }
    hex = c == ZHEX;
    crc_hi = crc_lo = 0;
    for (i = 0; i < 7; ++i) {
        c = hex ? hex_byte() : zget();
        if (c > 0xFF) {
            return c == STOPPED || c == Z_CANCEL ? c : Z_BAD;
        }
        if (i < 5) {
            hdr[i] = (uint8_t)c;
            crc((uint8_t)c);
        } else if ((uint8_t)c != (i == 5 ? crc_hi : crc_lo)) {
            return Z_BAD;
        }
    }
    return hdr[0];
}

static uint16_t zheader(void) { return zheader_from(0); }

/* A subpacket, into its bank: its end's letter (ZCRCE to ZCRCW), zlen
 * its length; or TIMEOUT, STOPPED, Z_BAD or Z_CANCEL. */
static uint16_t zsub(void) {
    uint16_t c, end;
    uint8_t got_hi;
    zlen = 0;
    crc_hi = crc_lo = 0;
    for (;;) {
        c = zget();
        if (c & FRAME_END) {
            end = c & 0xFF;
            crc((uint8_t)end);
            c = zget();
            if (c > 0xFF) {
                return c == STOPPED || c == Z_CANCEL ? c : Z_BAD;
            }
            got_hi = (uint8_t)c;
            c = zget();
            if (c > 0xFF) {
                return c == STOPPED || c == Z_CANCEL ? c : Z_BAD;
            }
            return got_hi == crc_hi && (uint8_t)c == crc_lo ? end : Z_BAD;
        }
        if (c > 0xFF) {
            return c;
        }
        if (zlen == ZMOST) {
            return Z_BAD;
        }
        crc((uint8_t)c);
        RAM_BANK = ZBANK;
        BANKED[zlen++] = (uint8_t)c;
        RAM_BANK = BANK_TRANSFER;
    }
}

/* the board told to stop, ZMODEM's way and XMODEM's */
static void zcancel(void) {
    uint8_t i;
    for (i = 0; i < 8; ++i) {
        uart_put(CAN);
    }
    for (i = 0; i < 8; ++i) {
        uart_put(0x08);
    }
    cancel();
}

/* the files, till ZFIN */
static void zreceive(void) {
    unsigned long pos = 0;
    uint8_t open = 0, quiet = 0, errors = 0;
    uint16_t t;

    zmodem = 1, size = 0, got = 0, name[0] = 0;
    progress(1);
    send_rinit();
    for (;;) {
        t = zheader();
        if (t == STOPPED || t == Z_CANCEL) {
            if (t == STOPPED) {
                zcancel();
            }
            if (open) {
                fail(t == STOPPED ? "stopped:" : "the board stopped:");
            } else {
                tell(t == STOPPED ? "stopped" : "the board stopped", 0);
            }
            return;
        }
        if (t == TIMEOUT || t == Z_BAD) {
            if (++quiet == 10 || ++errors == 30) {
                zcancel();
                if (open) {
                    fail("nothing more came:");
                } else {
                    tell("nothing came", 0);
                }
                return;
            }
            if (open) {
                send_pos(ZRPOS, pos);
            } else {
                send_rinit();
            }
            continue;
        }
        quiet = 0;
        switch (t) {
        case ZRQINIT:
            send_rinit();
            break;
        case ZSINIT:
            zsub(); /* its attention string: not needed */
            send_pos(ZACK, 0);
            break;
        case ZFILE:
            if (zsub() > 0xFF) {
                send_header(ZNAK);
                break;
            }
            if (open) {
                file_close(); /* the last one's ZEOF never came */
            }
            RAM_BANK = ZBANK;
            BANKED[zlen < 127 ? zlen : 127] = 0;
            take_name((const char *)BANKED);
            size = header_size();
            RAM_BANK = BANK_TRANSFER;
            if (!create()) {
                zcancel();
                tell("couldn't make", 0);
                return;
            }
            open = 1, pos = 0, got = 0;
            progress(1);
            send_pos(ZRPOS, 0);
            break;
        case ZDATA:
            if (!open) {
                send_rinit();
                break;
            }
            if (header_pos() != pos) {
                send_pos(ZRPOS, pos); /* the rest's passed over */
                break;
            }
            for (;;) {
                t = zsub();
                if (t > 0xFF) {
                    if (t == STOPPED || t == Z_CANCEL) {
                        if (t == STOPPED) {
                            zcancel();
                        }
                        fail(t == STOPPED ? "stopped:" : "the board stopped:");
                        return;
                    }
                    if (++errors == 30) {
                        zcancel();
                        fail("too many errors:");
                        return;
                    }
                    send_pos(ZRPOS, pos);
                    break;
                }
                RAM_BANK = ZBANK;
                file_write(BANKED, zlen);
                RAM_BANK = BANK_TRANSFER;
                pos += zlen;
                got = pos;
                progress(0);
                if (t == ZCRCQ || t == ZCRCW) {
                    send_pos(ZACK, pos);
                }
                if (t == ZCRCE || t == ZCRCW) {
                    break;
                }
            }
            break;
        case ZEOF:
            if (open && header_pos() == pos) {
                file_close();
                open = 0;
                progress(1);
                tell("received", 1);
                send_rinit();
            }
            break;
        case ZFIN:
            if (open) {
                fail("the board ended early:");
            }
            send_pos(ZFIN, 0);
            /* its "OO", over and out, after the end of its ZFIN */
            for (t = 0, quiet = 0; quiet < 8 && t < 2; ++quiet) {
                uint16_t c = get(SECOND);
                if (c > 0xFF) {
                    break;
                }
                t += c == 'O';
            }
            return;
        default:
            break;
        }
    }
}

/* ZMODEM's sending: a file of FILES, picked on the status line,
 * as a board's receiver (its ZRINIT) waits for one. Binary headers with a
 * 16-bit CRC, 1K subpackets streamed (ZCRCG), the board's ZRPOS heeded. */

/* the files listed, in their bank: a name and its size each */
#define LIST_BANK BANK_LIST
#define ENTRY 44 /* 40 of name, 4 of size */
#define MOST_FILES (BANK_SIZE / ENTRY)
static uint8_t listed;

/* a byte of a binary header or subpacket, ZDLE's escapes where needed */
static void zput(uint8_t c) {
    if (c == ZDLE || (c & 0x7F) == 0x10 || (c & 0x7F) == 0x11 || (c & 0x7F) == 0x13) {
        uart_put(ZDLE);
        uart_put(c ^ 0x40);
    } else {
        uart_put(c);
    }
}

/* a binary header: |type| and hdr[1] to hdr[4] */
static void send_binary(uint8_t type) {
    uint8_t i;
    hdr[0] = type;
    uart_put(ZPAD), uart_put(ZDLE), uart_put(ZBIN);
    crc_hi = crc_lo = 0;
    for (i = 0; i < 5; ++i) {
        crc(hdr[i]);
        zput(hdr[i]);
    }
    i = crc_lo;
    zput(crc_hi);
    zput(i);
}

static void binary_pos(uint8_t type, unsigned long pos) {
    hdr[1] = (uint8_t)pos, hdr[2] = (uint8_t)(pos >> 8);
    hdr[3] = (uint8_t)(pos >> 16), hdr[4] = (uint8_t)(pos >> 24);
    send_binary(type);
}

/* a subpacket of |n| bytes at |p|, in ZMODEM's bank if |banked|, ended
 * by |end| */
static void send_sub(const uint8_t *p, uint16_t n, uint8_t end, uint8_t banked) {
    uint8_t c, lo;
    crc_hi = crc_lo = 0;
    while (n--) {
        if (banked) {
            RAM_BANK = ZBANK;
            c = *p++;
            RAM_BANK = BANK_TRANSFER;
        } else {
            c = *p++;
        }
        crc(c);
        zput(c);
    }
    crc(end);
    uart_put(ZDLE);
    uart_put(end);
    lo = crc_lo;
    zput(crc_hi);
    zput(lo);
    if (end == ZCRCW) {
        uart_put(XON);
    }
}

/* FILES's files, from its long listing ("$=L": a BASIC program's
 * lines, each its size in K, the name in quotes, its type, its date,
 * and its size in bytes, 8 hex digits, last), into their bank */
static void list_files(void) {
    static char line[48];
    int16_t c;
    uint8_t n, i, quotes, fresh, first = 1;
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
        n = 0, quotes = 0, bytes = 0, fresh = 1;
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
            } else if (quotes == 2 && c == 'D' && n) {
                quotes = 3; /* DIR: a folder, not sent */
            }
        }
        if (c < 0) {
            break;
        }
        if (first) {
            first = 0; /* the card's own name */
            continue;
        }
        if (quotes != 2 || !n || listed == MOST_FILES) {
            continue;
        }
        line[n] = 0;
        RAM_BANK = LIST_BANK;
        for (i = 0; i <= n; ++i) {
            BANKED[listed * ENTRY + i] = (uint8_t)line[i];
        }
        *(unsigned long *)(BANKED + listed * ENTRY + 40) = bytes;
        RAM_BANK = BANK_TRANSFER;
        ++listed;
    }
    file_close();
}

/* file |i| of those listed, as |name| and |size| */
static void listed_file(uint8_t i) {
    uint8_t j;
    RAM_BANK = LIST_BANK;
    for (j = 0; j < 40 && (name[j] = (char)BANKED[i * ENTRY + j]); ++j) {
    }
    name[j] = 0;
    size = *(unsigned long *)(BANKED + i * ENTRY + 40);
    RAM_BANK = BANK_TRANSFER;
}

/* a file picked on the status line: its number, or -1 for none */
static int16_t pick(void) {
    static char line[96];
    uint8_t i = 0, k;
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
static uint8_t open_at(unsigned long pos) {
    char *p = put_text(spec, name);
    put_text(p, ",S,R")[0] = 0;
    file_close();
    if (!file_open(spec)) {
        return 0;
    }
    while (pos) {
        uint16_t n = pos > 1024 ? 1024 : (uint16_t)pos;
        RAM_BANK = ZBANK;
        n = file_read(BANKED, n);
        RAM_BANK = BANK_TRANSFER;
        if (!n) {
            return 0;
        }
        pos -= n;
    }
    return 1;
}

/* What the board's said while it's sent to, not waited for: a header,
 * if one's begun (a ZRPOS, say, or its CANs), else NOTHING_SAID */
#define NOTHING_SAID 0xFFFF
static uint16_t board_said(void) {
    uint8_t cans = 0;
    while (uart_ready()) {
        uint8_t c = uart_get();
        if (c == ZPAD) {
            return zheader_from(1);
        }
        if (c != CAN) {
            cans = 0;
        } else if (++cans == 5) {
            return Z_CANCEL;
        }
    }
    return NOTHING_SAID;
}

/* what's come while a file was picked thrown away, till a tenth of a
 * second's quiet */
static void quieten(void) {
    while (uart_get_within(SECOND / 10)) {
    }
}

/* a header the board sends, while it's sent: stopped (1) or not */
static uint8_t stopped_by(uint16_t t) {
    if (t == STOPPED || t == Z_CANCEL) {
        if (t == STOPPED) {
            zcancel();
        }
        file_close();
        tell(t == STOPPED ? "stopped sending" : "the board stopped", 0);
        return 1;
    }
    return 0;
}

static void zsend(void) {
    static uint8_t info[56];
    uint8_t tries, *q;
    uint16_t t, n;
    unsigned long pos = 0;

    zmodem = 1, sending = 1, got = 0;
    progress(1);
    quieten();

    /* the board's receiver, asked again to be sure it's there */
    for (tries = 0;; ++tries) {
        hdr[1] = hdr[2] = hdr[3] = hdr[4] = 0;
        send_header(ZRQINIT);
        t = zheader();
        if (t == ZRINIT) {
            break;
        }
        if (stopped_by(t)) {
            return;
        }
        if (tries == 10) {
            tell("the board didn't answer", 0);
            return;
        }
    }

    /* ZFILE: its name and size (its time left out: the X16's clock has
     * no time zone, and the board keeps its own); the board's ZRPOS,
     * where to start */
    q = (uint8_t *)put_text((char *)info, name);
    *q++ = 0;
    q = (uint8_t *)put_number((char *)q, size);
    *q++ = 0;
    for (tries = 0;; ++tries) {
        hdr[1] = hdr[2] = hdr[3] = 0;
        hdr[4] = ZCBIN;
        send_binary(ZFILE);
        send_sub(info, (uint16_t)(q - info), ZCRCW, 0);
        t = zheader();
        if (t == ZRPOS) {
            pos = header_pos();
            break;
        }
        if (t == ZSKIP) {
            tell("the board didn't want", 0);
            goto finish;
        }
        if (stopped_by(t)) {
            return;
        }
        if (tries == 10) {
            zcancel();
            tell("the board didn't answer", 0);
            return;
        }
    }

    for (;;) {
        /* its data, from |pos| */
        if (!open_at(pos)) {
            zcancel();
            tell("couldn't read", 0);
            return;
        }
        binary_pos(ZDATA, pos);
        for (;;) {
            RAM_BANK = ZBANK;
            n = file_read(BANKED, 1024);
            RAM_BANK = BANK_TRANSFER;
            send_sub(BANKED, n, n < 1024 ? ZCRCE : ZCRCG, 1);
            pos += n;
            got = pos;
            progress(0);
            if (keys_get() == KEY_ESC) {
                stopped_by(STOPPED);
                return;
            }
            if (n < 1024) {
                break;
            }
            /* the board's said something: a ZRPOS, to go back */
            t = board_said();
            if (stopped_by(t)) {
                return;
            }
            if (t == ZRPOS) {
                pos = header_pos();
                goto again;
            }
        }
        /* its end; the board's ZRINIT, all there, or ZRPOS */
        for (tries = 0;; ++tries) {
            binary_pos(ZEOF, pos);
            t = zheader();
            if (t == ZRINIT) {
                file_close();
                progress(1);
                tell("sent", 1);
                goto finish;
            }
            if (stopped_by(t)) {
                return;
            }
            if (t == ZRPOS) {
                pos = header_pos();
                break;
            }
            if (tries == 5) {
                zcancel();
                file_close();
                tell("the board didn't answer", 0);
                return;
            }
        }
    again:;
    }

finish:
    /* ZFIN each way, and "OO" */
    for (tries = 0; tries < 5; ++tries) {
        hdr[1] = hdr[2] = hdr[3] = hdr[4] = 0;
        send_header(ZFIN);
        if (zheader() == ZFIN) {
            /* its header's end, CR and LF, not for the screen */
            uart_get_within(SECOND / 2);
            uart_get_within(SECOND / 4);
            break;
        }
    }
    uart_put('O'), uart_put('O');
}

/* a file listed, picked and sent; or none, the board told so */
static void upload(const char *unused) {
    int16_t i;
    (void)unused;
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
    zsend();
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
        file_command("CD:..");
    } else {
        cancel();
        tell("couldn't open the FILES folder", 0);
    }
    RAM_BANK = bank;
}

static void zreceive_named(const char *unused) {
    (void)unused;
    zreceive();
}

void transfer_zmodem(void (*said)(const char *)) { in_downloads(zreceive_named, 0, said); }

void transfer_receive(const char *xname, void (*said)(const char *)) {
    in_downloads(receive, xname, said);
}

void transfer_upload(void (*said)(const char *)) { in_downloads(upload, 0, said); }
