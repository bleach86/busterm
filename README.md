# busterm

A BBS terminal for the Commander X16 and TexElec's Serial & ESP32 Network
Card. It's made for calling today's BBSes from the X16: their ANSI art in
the PC's character set and colours, at the card's full 115,200 baud.

## What it does so far

- **ANSI-BBS:** colours, cursor movement, clearing, scrolling regions,
  inserting and deleting lines, the PC's character set (code page 437) for
  box drawing and block art.
- **PETSCII:** for Commodore boards, 40 or 80 columns by 25, with the
  X16's own PETSCII font and the C64's colours; the keyboard types PETSCII.
  Each board in the directory says which it speaks.
- **A dialling directory and recent calls:** boards by name, address,
  terminal type and columns, kept on the SD card.
- **Tells the board its screen size:** 80 by 29, under busterm's status
  line.
- **Keeps up with the line:** the screen scrolls in VERA's hardware, so
  text pours in at the full 115,200 baud, short lines or long.
- **The modem:** the card's Zimodem, at 115,200 baud with RTS/CTS flow
  control, so nothing is lost when the screen falls behind.

Planned: scrollback, capture to a file, and XMODEM and YMODEM downloads.

## Using it

Load and run `BUSTERM.PRG`. It opens on the dialling directory: pick a
board and press Return to call it, or Esc for the modem's own prompt, where
`ATDT"bbs.example.org:23"` dials.

| Key   | Does                               |
| ----- | ---------------------------------- |
| Alt-D | The dialling directory             |
| Alt-E | ANSI, PETSCII 40 or PETSCII 80     |
| Alt-H | Hang up                            |
| Alt-S | Settings                           |
| Alt-Z | busterm's keys, on the status line |
| Alt-X | Quit                               |

In the directory: Return dials, N adds a board, E edits one, Del removes
it, and Tab shows the recent calls. Picking a board during a call asks before
hanging up and calling it. A call made from the directory goes back to it
when it ends; Esc closes it again to read the board's last words. A call
dialled at the modem's prompt goes back there.

The arrow keys and Home send ANSI codes, and Delete sends backspace.
busterm leaves the function keys alone, for programs and emulators that
use them.

### Files

busterm keeps two text files on the SD card, which a PC can edit too:

- `BUSTERM.DIR`, the directory, a board a line:
  `name|address|type|columns|calls|last called`
- `BUSTERM.RCT`, the last ten calls, newest first: `name|address|when`
- `BUSTERM.CFG`, the settings, a `name=value` line each: `start` (directory
  or modem), `backspace` (bs or del), `echo` (off or on), `cursor` (steady
  or blink), `bell` (beep or silent) and `clock` (24 or 12)

## Building

With the [llvm-mos SDK](https://github.com/llvm-mos/llvm-mos-sdk) in
`~/llvm-mos` (or `make LLVM_MOS=/path/to/sdk`):

```
make
```

`tools/mkfont.py` makes `src/font.c`, the character set, from Daniel
Hepper's public-domain [font8x8](https://github.com/dhepper/font8x8), kept
in `tools/font8x8/`.

## Licence

MIT. See [LICENSE](LICENSE). The font8x8 glyphs are public domain.
