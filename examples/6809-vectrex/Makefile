EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(EXAMPLE_DIR)build
VECTREX_DIR := $(EXAMPLE_DIR)../../src/rt/vectrex
VECTREX_LIB := $(VECTREX_DIR)/vectrex.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.rom

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --platform=6809-vectrex-raw --dump-ir --dump-asm --out="$@" "$<"

# vectrex.lib is src/rt/vectrex/'s own build product (see that directory's own Makefile) - built
# here via a sub-make rather than recompiling vectrex.c a second time, the same pattern
# examples/6809-coco/Makefile uses for coco.lib.
$(VECTREX_LIB):
	$(MAKE) -C $(VECTREX_DIR)

# The real GCE Vectrex cartridge ROM image - see ../../src/fileformat/vectrex/VectrexFormat.h.
$(BUILD_DIR)/main.rom: $(BUILD_DIR)/main.o $(VECTREX_LIB)
	$(RCC) --link --platform=6809-vectrex-vectrex --out="$@" "$(BUILD_DIR)/main.o" "$(VECTREX_LIB)"

# No verified Vectrex emulator is wired into this project's CI - see docs/known-bugs.md - so this
# only confirms the cartridge image was produced.
test: all
	test -s "$(BUILD_DIR)/main.rom"

clean:
	rm -rf "$(BUILD_DIR)"
