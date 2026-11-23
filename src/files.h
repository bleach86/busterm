#ifndef FILES_H
#define FILES_H

/* busterm's text files on the SD card, a line at a time, through the ROM's
 * own routines (llvm-mos has no cbm_open or cbm_read yet). One file is
 * open at a time. Names are as the ROM's OPEN takes them: "NAME,S,R" to
 * read, "@:NAME,S,W" to write over. */

#include <stdint.h>

/* opened to read; 0 if it isn't there */
uint8_t file_open(const char *name);

/* the next line into |buf| (room for |size| - 1 characters), without its
 * end; 0 when there are no more */
uint8_t file_line(char *buf, uint8_t size);

/* the folder's long listing ("$=L") opened, to read with file_byte() */
uint8_t file_listing(void);

/* the next byte of the file being read, or -1 at its end */
int16_t file_byte(void);

/* up to |n| bytes of the file being read into |p|, by the ROM's MACPTR,
 * a block at a time: how many there were (fewer at the end). The input's
 * the keyboard again after, for GETIN. */
uint16_t file_read(uint8_t *p, uint16_t n);

/* opened to write, over what was there; 0 if it can't be */
uint8_t file_create(const char *name);

/* text written to the file being written */
void file_put(const char *s);

/* |n| bytes written to the file being written */
void file_write(const uint8_t *p, uint16_t n);

/* a command to the drive, as "MD:NAME" or "CD:..": 1 if it said OK */
uint8_t file_command(const char *cmd);

/* the file closed, read or written */
void file_close(void);

/* the file |name| loaded at |at| in bank |bank| (going on into the banks
 * after it, as the ROM's LOAD does), its first two bytes (its own load
 * address) passed over: 1, or 0 if it couldn't be */
uint8_t file_load(const char *name, uint8_t bank, void *at);

#endif
