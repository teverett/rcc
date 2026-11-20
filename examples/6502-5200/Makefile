EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
ATARI5200_DIR := $(EXAMPLE_DIR)../../src/rt/atari5200
ATARI5200_LIB := $(ATARI5200_DIR)/atari5200.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.a5200

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --target=5200 --format=coff --dump-ir --dump-asm --out="$@" "$<"

# atari5200.lib is src/rt/atari5200/'s own build product (see that directory's Makefile) -
# built here via a sub-make rather than recompiling atari5200.c a second time, so this example
# links the same object every other atari5200.c consumer would.
$(ATARI5200_LIB):
	$(MAKE) -C $(ATARI5200_DIR)

# --base=$8000: a 16K cartridge image (see ../../src/fileformat/a5200/Atari5200RomFormat.h) -
# $B000/$A000/$8000/$4000 are the only four bases --format=a5200 accepts.
$(BUILD_DIR)/main.a5200: $(BUILD_DIR)/main.o $(ATARI5200_LIB)
	$(RCC) --link --target=5200 --format=a5200 --base='$$8000' --out="$@" "$(BUILD_DIR)/main.o" "$(ATARI5200_LIB)"

# Same link, written out as `raw` instead of `a5200` - identical program bytes minus the
# cartridge header - so CI can load and run it directly in py65 without having to parse the
# cartridge format first. Same --base as main.a5200 above - they need to stay identical bytes.
$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(ATARI5200_LIB)
	$(RCC) --link --target=5200 --format=raw --base='$$8000' --out="$@" "$(BUILD_DIR)/main.o" "$(ATARI5200_LIB)"

# Runs main.raw in py65's 6502 MPU core (test.py) - see that script's own header comment.
test: $(BUILD_DIR)/main.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"

clean:
	rm -rf "$(BUILD_DIR)"
