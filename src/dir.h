#ifndef DIR_H
#define DIR_H

/* The dialling directory and the recent calls, kept on the SD card as text
 * a PC can read and edit too:
 *
 *   BUSTERM.DIR   a board a line: name|address|type|columns|calls|last
 *   BUSTERM.RCT   a call a line, newest first: name|address|when
 *
 * type is ANSI, PETSCII or ASCII; when and last are YYMMDDhhmm. */

#include <stdint.h>

#include "banks.h"

#define DIR_MAX 80
#define RECENT_MAX 10
#define NAME_LEN 24
#define ADDR_LEN 48
#define WHEN_LEN 10

enum { EMU_ANSI, EMU_PETSCII, EMU_ASCII };

typedef struct {
    char name[NAME_LEN + 1];
    char addr[ADDR_LEN + 1]; /* host:port */
    uint8_t emu;
    uint8_t cols; /* 80 or 40 */
    uint16_t calls;
    char last[WHEN_LEN + 1];
} board_t;

typedef struct {
    char name[NAME_LEN + 1];
    char addr[ADDR_LEN + 1];
    char when[WHEN_LEN + 1];
} call_t;

_Static_assert(DIR_MAX * sizeof(board_t) + RECENT_MAX * sizeof(call_t) <= BANK_SIZE,
               "the boards and the recent calls fit their bank");

/* the boards and the recent calls, in banked RAM: bank BANK_DATA, which
 * busterm keeps selected */
#define dir ((board_t *)BANKED)
#define recent ((call_t *)(BANKED + DIR_MAX * sizeof(board_t)))
extern uint8_t dir_count;
extern uint8_t recent_count;

void dir_load(void);
void dir_save(void);

/* a call made: to the recent calls, and to its board's count, if it's one */
void dir_called(const char *name, const char *addr);

/* The directory's window. Returns the board to call, or 0 if it was
 * closed; a recent call not in the directory is ANSI, 80 columns. */
const board_t *dir_window(void);

#endif
