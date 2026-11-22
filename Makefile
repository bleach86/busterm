# busterm: built with llvm-mos (https://github.com/llvm-mos/llvm-mos-sdk).
# LLVM_MOS is where its SDK is.
LLVM_MOS ?= $(HOME)/llvm-mos
CC := $(LLVM_MOS)/bin/mos-cx16-clang
# -Oz: as small as it goes. It was as fast as -Os, measured, and room
# matters more: the program and its variables share $0801 to $9EFF with
# the stack.
CFLAGS := -Oz -Wall -Wextra

SRC := $(wildcard src/*.c) $(wildcard src/*.s)
HDR := $(wildcard src/*.h)

BUSTERM.PRG: $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC)

# The room left between the variables' end and the stack's top ($9F00),
# which the stack grows down into. The deepest it was seen to go is about
# 320 bytes (reading the dialling directory), so less than MIN_ROOM fails.
MIN_ROOM ?= 512
room: BUSTERM.PRG
	@symbol() { $(LLVM_MOS)/bin/llvm-nm BUSTERM.PRG.elf | awk -v s=$$1 '$$3 == s { print $$1 }'; }; \
	room=$$((0x$$(symbol __stack) - 0x$$(symbol __heap_start))); \
	echo "$$room bytes free for the stack (at least $(MIN_ROOM) wanted)"; \
	test $$room -ge $(MIN_ROOM)

clean:
	rm -f BUSTERM.PRG BUSTERM.PRG.elf

.PHONY: clean room
