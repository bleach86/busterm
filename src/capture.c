#include "capture.h"

#include "banks.h"
#include "files.h"

uint8_t capturing;

static char name[25];
static char spec[34];
static uint16_t fill; /* gathered in its bank */
static unsigned long total;
static uint8_t failed;

/* the FILES folder, made if it isn't there, as the drive's own */
static uint8_t in_folder(void) {
    file_command("MD:FILES");
    return file_command("CD:FILES");
}

/* the file, opened to add to (",A"), or, |make|, to write (",W") */
static uint8_t open_file(uint8_t make) {
    char *p = spec;
    const char *s = name;
    while (*s) {
        *p++ = *s++;
    }
    *p++ = ',', *p++ = 'S', *p++ = ',', *p++ = make ? 'W' : 'A';
    *p = 0;
    return file_create(spec);
}

/* what's gathered added to the file */
static void flush(void) {
    uint8_t bank = RAM_BANK, ok = 0;
    if (in_folder()) {
        if (open_file(0)) {
            RAM_BANK = BANK_CAPTURE;
            file_write(BANKED, fill);
            RAM_BANK = bank;
            file_close();
            ok = 1;
        }
        file_command("CD:..");
    }
    if (ok) {
        total += fill;
    } else {
        failed = 1;
        capturing = 0;
    }
    fill = 0;
}

uint8_t capture_start(const char *file) {
    uint8_t n = 0, ok = 0;
    while (file[n] && n < sizeof name - 1) {
        name[n] = file[n];
        ++n;
    }
    name[n] = 0;
    if (in_folder()) {
        /* there to add to, or made */
        ok = open_file(0) || open_file(1);
        file_close();
        file_command("CD:..");
    }
    fill = 0, total = 0, failed = 0;
    capturing = ok;
    return ok;
}

void capture_byte(uint8_t c) {
    uint8_t bank = RAM_BANK;
    RAM_BANK = BANK_CAPTURE;
    BANKED[fill] = c;
    RAM_BANK = bank;
    if (++fill == BANK_SIZE) {
        flush();
    }
}

void capture_flush(void) {
    if (capturing && fill) {
        flush();
    }
}

unsigned long capture_stop(uint8_t *ok) {
    capture_flush();
    capturing = 0;
    *ok = !failed;
    return total;
}

const char *capture_name(void) { return name; }
