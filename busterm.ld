/* busterm's link: the SDK's for the X16 (its cx16 link.ld), with the
 * banked RAM at $A000 besides. The program, BUSTERM.PRG, is what's in
 * main RAM, $0801 on; what's in .bank (ZMODEM's code, a stamp first) is cut
 * out by the Makefile as BUSTERM.BNK, which busterm loads into BANK_CODE as
 * it starts. */

__basic_zp_start = 0x0002;
__basic_zp_end = 0x0080;

MEMORY {
    ram (rw) : ORIGIN = 0x0801, LENGTH = 0x96ff
    bank (rwx) : ORIGIN = 0xA000, LENGTH = 0x2000
}

SECTIONS {
    .bank : {
        KEEP(*(.bank.0stamp))
        *(.bank .bank.*)
    } >bank
}

INCLUDE commodore.ld

/* Set initial soft stack address to end of BASIC area. (It grows down.) */
__stack = 0x9f00;

OUTPUT_FORMAT {
    /* Tells the LOAD command where to place the file's contents. */
    SHORT(ORIGIN(ram))
    TRIM(ram)
}
