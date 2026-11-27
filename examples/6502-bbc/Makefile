EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
BBC_DIR := $(EXAMPLE_DIR)../../src/rt/bbc
BBC_LIB := $(BBC_DIR)/bbc.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.ssd

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --debug-map --platform=6502-ssd --dump-ir --dump-asm --out="$@" "$<"

# bbc.lib is src/rt/bbc/'s own build product (see that directory's own
# Makefile) - built here via a sub-make rather than recompiling bbc.c a
# second time, so this example links the same object every other bbc.c
# consumer would.
$(BBC_LIB):
	$(MAKE) -C $(BBC_DIR)

# --base=$1900: PAGE, the BBC Micro's own user-program start address once
# DFS (the disc filing system) is in use - DFS's own workspace sits just
# below it, between the OS's fixed $0E00 and $1900 (see docs/platforms.md's
# own "bbc" entry for the memory-map sources this is taken from). The
# default $0600 base instead sits inside the frame-storage region
# M6502Codegen.h's own `kFrameStackBase` reserves ($0200-$05FF), not this
# machine's program area at all.
$(BUILD_DIR)/main.ssd: $(BUILD_DIR)/main.o $(BBC_LIB)
	$(RCC) --debug-map --link --platform=6502-ssd --base='$$1900' --out="$@" "$(BUILD_DIR)/main.o" "$(BBC_LIB)"

# Same link, written out as `raw` instead of `ssd` - identical program
# bytes minus the disc image's own catalogue sectors (see ../../src/
# fileformat/dfs/DfsFormat.h) - so CI can load and run it directly in py65
# without having to parse the catalogue first. Same --base as main.ssd
# above - they need to stay identical bytes.
$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(BBC_LIB)
	$(RCC) --link --platform=6502-raw --base='$$1900' --out="$@" "$(BUILD_DIR)/main.o" "$(BBC_LIB)"

# Runs main.raw in py65's 6502 MPU core (test.py) - see that script's own
# header comment.
test: $(BUILD_DIR)/main.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"

clean:
	rm -rf "$(BUILD_DIR)"
