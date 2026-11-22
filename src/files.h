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

/* opened to write, over what was there; 0 if it can't be */
uint8_t file_create(const char *name);

/* text written to the file being written */
void file_put(const char *s);

/* the file closed, read or written */
void file_close(void);

#endif
