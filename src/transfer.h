#ifndef TRANSFER_H
#define TRANSFER_H

/* Downloads: ZMODEM, started by the board; XMODEM (CRC or checksum, 128
 * or 1K blocks) and YMODEM batch, told apart by the sender's first block;
 * into the SD card's FILES
 * folder (made if it isn't there), so no board can write over busterm's
 * own files. Its progress is on the status line, on any screen. */

#include <stdint.h>

/* ZMODEM's code loaded (from BUSTERM.BNK, into its bank), as busterm
 * starts; and whether it was, the one made with this busterm */
void transfer_init(void);
uint8_t transfer_ready(void);

/* Files received, the board sending already or soon: XMODEM's named
 * |xname| (YMODEM names its own). |say| is told of each file, as a line
 * for the terminal. Esc stops it. */
void transfer_receive(const char *xname, void (*say)(const char *));

/* ZMODEM's files received, the board having begun ("**", ZDLE, "B00": its
 * ZRQINIT, which busterm watches for), into the same folder */
void transfer_zmodem(void (*say)(const char *));

/* A file of FILES sent by ZMODEM, picked on the status line, as the
 * board's receiver waits for it (its ZRINIT, "**", ZDLE, "B01", watched
 * for too) */
void transfer_upload(void (*say)(const char *));

/* A file of FILES sent by YMODEM (|ymodem|) or XMODEM, picked on the
 * status line, as the board's receiver waits for it, having asked (|ask|:
 * 'C', NAK or 'G', seen already, as busterm watches for it) */
void transfer_send(void (*say)(const char *), uint8_t ymodem, uint8_t ask);

#endif
