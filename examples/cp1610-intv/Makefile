EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(EXAMPLE_DIR)build
INTV_DIR := $(EXAMPLE_DIR)../../src/rt/intv
INTV_LIB := $(INTV_DIR)/intv.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.bin

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --platform=cp1610-intv-raw --dump-ir --dump-asm --out="$@" "$<"

# intv.lib is src/rt/intv/'s own build product (see that directory's own Makefile) - built here
# via a sub-make rather than recompiling intv.c a second time, the same convention
# ../6809-coco/Makefile's own COCO_LIB uses.
$(INTV_LIB):
	$(MAKE) -C $(INTV_DIR)

# The real Intellivision cartridge ROM image - see ../../src/fileformat/intvbin/IntvBinFormat.h.
$(BUILD_DIR)/main.bin: $(BUILD_DIR)/main.o $(INTV_LIB)
	$(RCC) --link --platform=cp1610-intv-intvbin --out="$@" "$(BUILD_DIR)/main.o" "$(INTV_LIB)"

# No accessible, simulator-runnable Intellivision machine exists to run this against - jzIntv
# needs copyrighted EXEC/GROM ROM images this project can't bundle or install - see
# docs/known-bugs.md. This only confirms the cartridge image exists and is non-empty.
test: all
	test -s "$(BUILD_DIR)/main.bin"

clean:
	rm -rf "$(BUILD_DIR)"
