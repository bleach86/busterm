#include "files.h"

#include <cbm.h>
#include <cx16.h>

#define SD 8
#define LFN 2
#define CMD 15
#define ST_EOF 0x40

/* the file being read has no more */
static uint8_t at_end;

/* a command to the drive (or none), and its status: 1 for "00, OK" */
static uint8_t command(const char *cmd) {
    uint8_t a, b;
    cbm_k_setlfs(CMD, SD, CMD);
    cbm_k_setnam(cmd);
    if (cbm_k_open()) {
        cbm_k_close(CMD);
        return 0;
    }
    cbm_k_chkin(CMD);
    a = cbm_k_chrin();
    b = cbm_k_chrin();
    while (!(cbm_k_readst() & ST_EOF) && cbm_k_chrin() != 0x0D) {
    }
    cbm_k_clrch();
    cbm_k_close(CMD);
    return a == '0' && b == '0';
}

static uint8_t drive_ok(void) { return command(""); }

uint8_t file_command(const char *cmd) { return command(cmd); }

uint8_t file_open(const char *name) {
    cbm_k_setlfs(LFN, SD, 2);
    cbm_k_setnam(name);
    if (cbm_k_open() || !drive_ok() || cbm_k_chkin(LFN)) {
        cbm_k_clrch();
        cbm_k_close(LFN);
        at_end = 1;
        return 0;
    }
    at_end = 0;
    return 1;
}

uint8_t file_line(char *buf, uint8_t size) {
    uint8_t n = 0;
    if (at_end) {
        return 0;
    }
    for (;;) {
        uint8_t c = cbm_k_chrin();
        uint8_t st = cbm_k_readst();
        if (st & ~ST_EOF) {
            at_end = 1; /* an error: what's read so far */
            break;
        }
        if (c == '\n') {
            if (st & ST_EOF) {
                at_end = 1;
            }
            break;
        }
        if (c != '\r' && n < size - 1) {
            buf[n++] = (char)c;
        }
        if (st & ST_EOF) {
            at_end = 1;
            break;
        }
    }
    buf[n] = 0;
    return n || !at_end;
}

uint8_t file_listing(void) {
    cbm_k_setlfs(LFN, SD, 0);
    cbm_k_setnam("$=L");
    if (cbm_k_open() || cbm_k_chkin(LFN)) {
        cbm_k_clrch();
        cbm_k_close(LFN);
        at_end = 1;
        return 0;
    }
    at_end = 0;
    return 1;
}

int16_t file_byte(void) {
    uint8_t c, st;
    if (at_end) {
        return -1;
    }
    c = cbm_k_chrin();
    st = cbm_k_readst();
    if (st & ~ST_EOF) {
        at_end = 1;
        return -1;
    }
    if (st & ST_EOF) {
        at_end = 1;
    }
    return c;
}

uint16_t file_read(uint8_t *p, uint16_t n) {
    uint16_t got = 0;
    /* the file the input only while it's read: GETIN, the keyboard's,
     * reads whatever the input is */
    if (cbm_k_chkin(LFN)) {
        return 0;
    }
    while (got < n && !at_end) {
        uint16_t want = n - got > 255 ? 255 : n - got;
        long read = cx16_k_macptr((uint8_t)want, 0, p + got);
        uint8_t st = cbm_k_readst();
        if (read < 0) {
            /* no MACPTR: a byte at a time */
            int16_t c = file_byte();
            if (c < 0) {
                break;
            }
            p[got++] = (uint8_t)c;
            continue;
        }
        got += (uint16_t)read;
        if (st & ST_EOF || st & ~ST_EOF || !read) {
            at_end = 1;
        }
    }
    cbm_k_clrch();
    return got;
}

uint8_t file_create(const char *name) {
    cbm_k_setlfs(LFN, SD, 2);
    cbm_k_setnam(name);
    if (cbm_k_open() || !drive_ok() || cbm_k_chkout(LFN)) {
        cbm_k_clrch();
        cbm_k_close(LFN);
        return 0;
    }
    return 1;
}

void file_put(const char *s) {
    while (*s) {
        cbm_k_chrout((uint8_t)*s++);
    }
}

/* the ROM's MCIOUT: up to |n| bytes from |p| to the file, a block at a
 * time; how many it took, or 0 if it can't (then CHROUT's used) */
static uint8_t mciout(const uint8_t *p, uint8_t n) {
    uint8_t lo = (uint8_t)(uint16_t)p, hi = (uint8_t)((uint16_t)p >> 8), failed;
    __asm__ volatile("clc\n"
                     "jsr $feb1\n"
                     "lda #0\n"
                     "rol\n"
                     : "=a"(failed), "+x"(lo), "+y"(hi)
                     : "a"(n)
                     : "c", "v", "memory");
    return failed ? 0 : lo;
}

void file_write(const uint8_t *p, uint16_t n) {
    while (n) {
        uint8_t done = mciout(p, n > 128 ? 128 : (uint8_t)n);
        if (!done) {
            break;
        }
        p += done, n -= done;
    }
    while (n--) {
        cbm_k_chrout(*p++);
    }
}

void file_close(void) {
    cbm_k_clrch();
    cbm_k_close(LFN);
}
