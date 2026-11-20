EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
MSX_DIR := $(EXAMPLE_DIR)../../src/rt/msx
MSX_LIB := $(MSX_DIR)/msx.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.msx

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --platform=z80-msx-msxrom --dump-ir --dump-asm --out="$@" "$<"

# msx.lib is src/rt/msx/'s own build product (see that directory's own Makefile) - built here via
# a sub-make rather than recompiling msx.c a second time.
$(MSX_LIB):
	$(MAKE) -C $(MSX_DIR)

$(BUILD_DIR)/main.msx: $(BUILD_DIR)/main.o $(MSX_LIB)
	$(RCC) --link --platform=z80-msx-msxrom --out="$@" "$(BUILD_DIR)/main.o" "$(MSX_LIB)"

# Same link, written out as `raw` instead of `msxrom` - identical bytes minus msxrom's own
# padding up to a standard cartridge size - so CI can load and run it directly in a Z80
# simulator without padding to worry about.
$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(MSX_LIB)
	$(RCC) --link --platform=z80-msx-raw --out="$@" "$(BUILD_DIR)/main.o" "$(MSX_LIB)"

# Runs main.raw on a real Z80 core (test.py) - see that script's own header comment.
test: $(BUILD_DIR)/main.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"

clean:
	rm -rf "$(BUILD_DIR)"
