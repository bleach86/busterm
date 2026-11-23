# busterm

A BBS terminal for the Commander X16 and TexElec's Serial & ESP32 Network
Card. It's made for calling today's BBSes from the X16: their ANSI art in
the PC's character set and colours, at the card's full 115,200 baud.

It needs the X16's R49 ROM (or later) and the card, with its Zimodem
firmware. So far it's been tested in the
[Open Bus](https://github.com/bleach86/Open-Bus) emulator, whose X16 has
the card and Zimodem, and not yet on real hardware: reports from a real X16
are welcome.

## Getting it

`BUSTERM.PRG` is on the [releases](../../releases) page, alone or in a zip
with this README. Put it on the X16's SD card, then `LOAD "BUSTERM.PRG"` and
`RUN`.

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
- **Scrollback:** about 2,800 rows that scrolled off the top, colours and
  all, kept in the X16's banked RAM. Page Up (or Alt-B) looks back.
- **Downloads:** ZMODEM, started by itself when the board starts sending
  (about 10 KB a second); YMODEM batch (YMODEM-g when the sender can) and
  XMODEM (CRC or checksum, 128 or 1K blocks). Into a `FILES` folder on the
  SD card.
- **Uploads:** ZMODEM, started by itself when the board asks for a file:
  pick one of `FILES` on the status line, and it's sent (about 10 KB
  a second).
- **Capture:** what the board sends, its ANSI or PETSCII codes and all,
  added to a file in `FILES`, so a screen captured is an `.ANS` (or
  `.SEQ`) file.
- **Keeps up with the line:** the screen scrolls in VERA's hardware, so
  text pours in at the full 115,200 baud, short lines or long.
- **The modem:** the card's Zimodem, at 115,200 baud with RTS/CTS flow
  control, so nothing is lost when the screen falls behind.

## Using it

It opens on the dialling directory: pick a
board and press Return to call it, or Esc for the modem's own prompt, where
`ATDT"bbs.example.org:23"` dials.

| Key   | Does                               |
| ----- | ---------------------------------- |
| PgUp  | Scrollback (also Alt-B)            |
| Alt-C | Capture to a file, or stop         |
| Alt-D | The dialling directory             |
| Alt-E | ANSI, PETSCII 40 or PETSCII 80     |
| Alt-H | Hang up                            |
| Alt-R | Receive a YMODEM or XMODEM file    |
| Alt-S | Settings                           |
| Alt-Z | busterm's keys, on the status line |
| Alt-X | Quit                               |

The first time, the directory has a few boards to start with: Open Bus (ANSI
and PETSCII), The Oasis BBS and Borderline BBS (PETSCII) and Level 29
(ANSI).

In the directory: Return dials, N adds a board, E edits one, Del removes
it, and Tab shows the recent calls. Picking a board during a call asks before
hanging up and calling it. A call made from the directory goes back to it
when it ends; Esc closes it again to read the board's last words. After a
PETSCII call, busterm asks first: any key opens the directory, and Esc
stays with the board's last words. A call dialled at the modem's prompt goes
back there. Opening the directory during a PETSCII call shows it in ANSI;
closing it puts the call's screen back to PETSCII.

To download, pick the file on the board and choose ZMODEM: busterm sees it
start and receives it, nothing to press. With YMODEM, whose sender says
nothing to start, press Alt-R once the board's waiting; XMODEM from lrzsz
(as most boards have) starts by itself too, its file named DOWNLOAD.BIN.
The progress shows on the status line, and Esc stops it.

To upload, choose upload on the board, with ZMODEM: busterm sees the board
waiting and asks on the status line which file of `FILES` to send (Up
and Down go through them, with their sizes; Return sends it; Esc sends
none, and tells the board so). Files go in a `FILES` folder next to
busterm, made the first time, so no board can write over busterm's own
files; one of the same name is replaced. In Open Bus,
`FILES` is a folder on the computer (Documents, `Open Bus/X16 Files`, as
it starts), the same on every X16 program's card. A file stopped
halfway is deleted. XMODEM pads a file's last block to 128 bytes (with
Ctrl-Z), and that's kept; YMODEM sends the size, so its files are exact.

To capture, press Alt-C and give a file name (or Return for the last).
Everything the board sends from then on goes to the end of that file, in
`FILES`, made if it isn't there; the status line says `capturing` (a `*`
before the board's name at 40 columns). Alt-C again stops it, and says how
much was captured. Quitting stops it too.

In scrollback: Up and Down move a row, Page Up and Page Down a screen, Home
goes to the oldest row, and Esc (or Page Down at the bottom) goes back to
the terminal. The board waits meanwhile. Changing between ANSI and PETSCII
starts the scrollback afresh.

The arrow keys and Home send ANSI codes, and Delete sends backspace.
busterm leaves the function keys alone, for programs and emulators that
use them.

### Files

busterm keeps three text files on the SD card, which a PC can edit too:

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

The PC's character set is the X16 ROM's own (its CP437 set, which R49
has).

busterm and its variables share the X16's main RAM with the stack, which
grows down from $9F00. `make room` says how much is left for the stack and
fails under 512 bytes. The checks on GitHub run it, with the build (warnings
as errors) and the formatting (`clang-format` from the SDK v23.4.0, Prettier
3.7.4), and keep each build's `BUSTERM.PRG`.

## Licence

MIT. See [LICENSE](LICENSE).
