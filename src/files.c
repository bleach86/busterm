#include "files.h"

#include <cbm.h>

#define SD 8
#define LFN 2
#define CMD 15
#define ST_EOF 0x40

/* the file being read has no more */
static uint8_t at_end;

/* the drive's status: "00, OK" */
static uint8_t drive_ok(void) {
    uint8_t a, b;
    cbm_k_setlfs(CMD, SD, CMD);
    cbm_k_setnam("");
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

uint8_t file_create(const char *name) {
    cbm_k_setlfs(LFN, SD, 2);
    cbm_k_setnam(name);
    if (cbm_k_open() || cbm_k_chkout(LFN)) {
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

void file_close(void) {
    cbm_k_clrch();
    cbm_k_close(LFN);
}
