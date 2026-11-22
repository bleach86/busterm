# busterm: built with llvm-mos (https://github.com/llvm-mos/llvm-mos-sdk).
# LLVM_MOS is where its SDK is.
LLVM_MOS ?= $(HOME)/llvm-mos
CC := $(LLVM_MOS)/bin/mos-cx16-clang
CFLAGS := -Os -Wall -Wextra

SRC := $(wildcard src/*.c) $(wildcard src/*.s)
HDR := $(wildcard src/*.h)

BUSTERM.PRG: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# the font, from tools/font8x8
src/font.c: tools/mkfont.py
	python3 tools/mkfont.py

clean:
	rm -f BUSTERM.PRG BUSTERM.PRG.elf

.PHONY: clean
