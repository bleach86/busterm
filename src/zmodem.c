/* ZMODEM, receiving and sending: in banked RAM (BANK_CODE), loaded from
 * BUSTERM.BNK, so main RAM has room for the rest. It runs with that bank
 * picked and never picks another: ZMODEM's subpacket bank is reached
 * through zbank_put and the rest, in main RAM (see xfer.h). */

#include <cx16.h>

#include "files.h"
#include "keys.h"
#include "uart.h"
#include "xfer.h"

/* the bank's first: what it's of, checked as busterm loads it */
__attribute__((section(".bank.0stamp"), used)) const char bank_stamp[] = "busterm " BUILD_ID;

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

/* a subpacket: up to 8K, in its own bank (zbank_put and the rest) */
#define ZMOST 8192

static uint8_t hdr[5]; /* a header's type and its 4 bytes */
static uint16_t zlen;  /* a subpacket's length */

/* the CRC's table in two halves, high bytes and low, here in the bank */
static uint8_t crc_table[512] __attribute__((section(".bank.data")));

IN_BANK static void ztables(void) {
    uint8_t i = 0, j;
    do {
        uint16_t c = (uint16_t)i << 8;
        for (j = 0; j < 8; ++j) {
            c = c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1;
        }
        crc_table[i] = (uint8_t)(c >> 8);
        crc_table[256 + i] = (uint8_t)c;
    } while (++i);
}

/* the CRC kept a byte on */
static uint8_t crc_hi, crc_lo;
IN_BANK static void crc(uint8_t c) {
    uint8_t idx = crc_hi ^ c;
    crc_hi = crc_lo ^ crc_table[idx];
    crc_lo = crc_table[256 + idx];
}

IN_BANK static void put_hex(uint8_t b) {
    static const char digits[] = "0123456789abcdef";
    uart_put(digits[b >> 4]);
    uart_put(digits[b & 15]);
}

/* a hex header: |type| and its 4 bytes as hdr[1] to hdr[4] */
IN_BANK static void send_header(uint8_t type) {
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
IN_BANK static void send_pos(uint8_t type, unsigned long pos) {
    hdr[1] = (uint8_t)pos, hdr[2] = (uint8_t)(pos >> 8);
    hdr[3] = (uint8_t)(pos >> 16), hdr[4] = (uint8_t)(pos >> 24);
    send_header(type);
}

IN_BANK static void send_rinit(void) {
    hdr[1] = hdr[2] = hdr[3] = 0; /* no buffer's limit: stream */
    hdr[4] = CANFDX | CANOVIO;
    send_header(ZRINIT);
}

IN_BANK static unsigned long header_pos(void) {
    return hdr[1] | (uint16_t)hdr[2] << 8 | (unsigned long)hdr[3] << 16 |
           (unsigned long)hdr[4] << 24;
}

/* a byte, XON and XOFF passed over */
IN_BANK static uint16_t zraw(void) {
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
IN_BANK static uint16_t zget(void) {
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

IN_BANK static uint16_t hex_digit(uint16_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return c > 0xFF ? c : Z_BAD;
}

/* a hex header's byte */
IN_BANK static uint16_t hex_byte(void) {
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
IN_BANK static uint16_t zheader_from(uint8_t pads) {
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

IN_BANK static uint16_t zheader(void) { return zheader_from(0); }

/* A subpacket, into its bank: its end's letter (ZCRCE to ZCRCW), zlen
 * its length; or TIMEOUT, STOPPED, Z_BAD or Z_CANCEL. */
IN_BANK static uint16_t zsub(void) {
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
        zbank_put(zlen++, (uint8_t)c);
    }
}

/* the files, till ZFIN */
FAR void zreceive(void) {
    unsigned long pos = 0;
    uint8_t open = 0, quiet = 0, errors = 0;
    uint16_t t;

    zmodem = 1, size = 0, got = 0, name[0] = 0;
    ztables();
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
            zbank_file(zlen);
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
                zbank_write(zlen);
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

/* a byte of a binary header or subpacket, ZDLE's escapes where needed */
IN_BANK static void zput(uint8_t c) {
    if (c == ZDLE || (c & 0x7F) == 0x10 || (c & 0x7F) == 0x11 || (c & 0x7F) == 0x13) {
        uart_put(ZDLE);
        uart_put(c ^ 0x40);
    } else {
        uart_put(c);
    }
}

/* a binary header: |type| and hdr[1] to hdr[4] */
IN_BANK static void send_binary(uint8_t type) {
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

IN_BANK static void binary_pos(uint8_t type, unsigned long pos) {
    hdr[1] = (uint8_t)pos, hdr[2] = (uint8_t)(pos >> 8);
    hdr[3] = (uint8_t)(pos >> 16), hdr[4] = (uint8_t)(pos >> 24);
    send_binary(type);
}

/* a subpacket of |n| bytes ended by |end|: from |p|, or with no |p| from
 * ZMODEM's bank */
IN_BANK static void send_sub(const uint8_t *p, uint16_t n, uint8_t end) {
    uint8_t c, lo;
    uint16_t at = 0;
    crc_hi = crc_lo = 0;
    while (n--) {
        c = p ? *p++ : zbank_get(at++);
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

/* What the board's said while it's sent to, not waited for: a header,
 * if one's begun (a ZRPOS, say, or its CANs), else NOTHING_SAID */
#define NOTHING_SAID 0xFFFF
IN_BANK static uint16_t board_said(void) {
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
IN_BANK static void quieten(void) {
    while (uart_get_within(SECOND / 10)) {
    }
}

/* a header the board sends, while it's sent: stopped (1) or not */
IN_BANK static uint8_t stopped_by(uint16_t t) {
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

FAR void zsend(void) {
    static uint8_t info[56];
    uint8_t tries, *q;
    uint16_t t, n;
    unsigned long pos = 0;

    zmodem = 1, sending = 1, got = 0;
    ztables();
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
        send_sub(info, (uint16_t)(q - info), ZCRCW);
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
            n = zbank_read(1024);
            send_sub(0, n, n < 1024 ? ZCRCE : ZCRCG);
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
