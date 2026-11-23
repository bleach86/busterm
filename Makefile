# busterm: built with llvm-mos (https://github.com/llvm-mos/llvm-mos-sdk).
# LLVM_MOS is where its SDK is.
LLVM_MOS ?= $(HOME)/llvm-mos
CC := $(LLVM_MOS)/bin/mos-cx16-clang
OBJCOPY := $(LLVM_MOS)/bin/llvm-objcopy
OBJDUMP := $(LLVM_MOS)/bin/llvm-objdump
NM := $(LLVM_MOS)/bin/llvm-nm
# -Oz: as small as it goes. It was as fast as -Os, measured, and room
# matters more: the program and its variables share $0801 to $9EFF with
# the stack.
CFLAGS := -Oz -Wall -Wextra

SRC := $(wildcard src/*.c) $(wildcard src/*.s)
HDR := $(wildcard src/*.h)

# The build, as its sources make it: BUSTERM.BNK is stamped with it, and
# busterm loads only the one with its own stamp.
BUILD_ID := $(shell cat $(SRC) $(HDR) busterm.ld | md5sum | cut -c1-8)

# busterm: BUSTERM.PRG, and BUSTERM.BNK beside it, ZMODEM's code for its
# bank (busterm.ld, src/xfer.h).
all: BUSTERM.PRG BUSTERM.BNK

BUSTERM.PRG: $(SRC) $(HDR) busterm.ld
	$(CC) $(CFLAGS) -DBUILD_ID='"$(BUILD_ID)"' -T busterm.ld -o $@ $(SRC)

# the bank's part, with a load address ($A000) first, as a PRG has
BUSTERM.BNK: BUSTERM.PRG
	$(OBJCOPY) -O binary --only-section=.bank BUSTERM.PRG.elf $@.bin
	printf '\000\240' | cat - $@.bin > $@
	rm -f $@.bin

# The room left between the variables' end and the stack's top ($9F00),
# which the stack grows down into. The deepest it was seen to go is about
# 320 bytes (reading the dialling directory), so less than MIN_ROOM fails.
MIN_ROOM ?= 512
room: BUSTERM.PRG
	@symbol() { $(NM) BUSTERM.PRG.elf | awk -v s=$$1 '$$3 == s { print $$1 }'; }; \
	room=$$((0x$$(symbol __stack) - 0x$$(symbol __heap_start))); \
	echo "$$room bytes free for the stack (at least $(MIN_ROOM) wanted)"; \
	test $$room -ge $(MIN_ROOM)

# The bank's code never picks a bank (RAM_BANK, at $00): it would take
# itself away. Its functions (not its data) are looked through for a store
# to $00.
bankcheck: BUSTERM.PRG
	@fns=$$($(OBJDUMP) -t BUSTERM.PRG.elf | awk '$$3 == "F" && $$4 ~ /^\.bank/ { printf "%s%s", sep, $$NF; sep = "," }'); \
	test -n "$$fns" || { echo "no functions in the bank"; exit 1; }; \
	picks=$$($(OBJDUMP) -d --disassemble-symbols=$$fns BUSTERM.PRG.elf | grep -E '\bst[axyz][[:space:]]+\$$0([[:space:]]|$$)' || true); \
	if [ -n "$$picks" ]; then echo "a bank picked in the bank:"; echo "$$picks"; exit 1; fi; \
	echo "the bank's code picks no bank ($$(echo $$fns | tr ',' ' ' | wc -w) functions)"

clean:
	rm -f BUSTERM.PRG BUSTERM.PRG.elf BUSTERM.BNK

.PHONY: all clean room bankcheck
