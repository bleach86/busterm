#ifndef TRANSFER_H
#define TRANSFER_H

/* Downloads: ZMODEM, started by the board; XMODEM (CRC or checksum, 128
 * or 1K blocks) and YMODEM batch, told apart by the sender's first block;
 * into the SD card's DOWNLOADS
 * folder (made if it isn't there), so no board can write over busterm's
 * own files. Its progress is on the status line, on any screen. */

#include <stdint.h>

/* Files received, the board sending already or soon: XMODEM's named
 * |xname| (YMODEM names its own). |say| is told of each file, as a line
 * for the terminal. Esc stops it. */
void transfer_receive(const char *xname, void (*say)(const char *));

/* ZMODEM's files received, the board having begun ("**", ZDLE, "B00": its
 * ZRQINIT, which busterm watches for), into the same folder */
void transfer_zmodem(void (*say)(const char *));

#endif
