#ifndef BANKS_H
#define BANKS_H

/* The X16's banked RAM: 8 KB at $A000, the bank picked at $00; every X16
 * has banks 0 to 63. Bank 0 is the ROM's. busterm keeps ZMODEM's code in
 * bank 2 (BUSTERM.BNK, loaded as it starts), its data (the
 * directory) in bank 3, which is left selected, the screen under its
 * windows in bank 1, a download's blocks in banks 4 and 6, what's
 * captured in bank 5, the files to send listed in bank 7, and the
 * scrollback in banks 8 to 63; those are
 * picked only while they're used. The ROM's routines leave the bank as
 * they found it. */

#include <cx16.h> /* RAM_BANK */
#include <stdint.h>

#define BANKED ((uint8_t *)0xA000)
#define BANK_SIZE 8192

#define BANK_WINDOWS 1
#define BANK_CODE 2 /* ZMODEM's code (see xfer.h) */
#define BANK_DATA 3
#define BANK_TRANSFER 4 /* a download's block, and its CRC table */
#define BANK_CAPTURE 5  /* what's captured, till it's written */
#define BANK_ZMODEM 6   /* a ZMODEM subpacket, up to 8K */
#define BANK_LIST 7     /* the files to send, listed */
#define BANK_SCROLLBACK 8
#define BANK_SCROLLBACK_END 64 /* the first past it */

#endif
