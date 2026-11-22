#ifndef BANKS_H
#define BANKS_H

/* The X16's banked RAM: 8 KB at $A000, the bank picked at $00. Bank 0 is
 * the ROM's. busterm keeps its data (the directory) in bank 3, which is
 * left selected, and the screen under its windows in bank 1, picked only
 * while it's kept or put back. The ROM's routines leave the bank as they
 * found it. */

#include <cx16.h> /* RAM_BANK */
#include <stdint.h>

#define BANKED ((uint8_t *)0xA000)
#define BANK_SIZE 8192

#define BANK_WINDOWS 1
#define BANK_DATA 3

#endif
