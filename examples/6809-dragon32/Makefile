EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(EXAMPLE_DIR)build
DRAGON32_DIR := $(EXAMPLE_DIR)../../src/rt/dragon32
DRAGON32_LIB := $(DRAGON32_DIR)/dragon32.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.raw $(BUILD_DIR)/main.decb $(BUILD_DIR)/api_test.raw

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --platform=6809-raw --dump-ir --dump-asm --out="$@" "$<"

$(BUILD_DIR)/api_test.o: api_test.c | $(BUILD_DIR)
	$(RCC) --platform=6809-raw --dump-ir --dump-asm --out="$@" "$<"

# dragon32.lib is src/rt/dragon32/'s own build product (see that directory's own Makefile) -
# built here via a sub-make rather than recompiling dragon32.c a second time, the same pattern
# examples/6809-coco/Makefile uses for coco.lib.
$(DRAGON32_LIB):
	$(MAKE) -C $(DRAGON32_DIR)

$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(DRAGON32_LIB)
	$(RCC) --link --platform=6809-raw --out="$@" "$(BUILD_DIR)/main.o" "$(DRAGON32_LIB)"

# The real Dragon/CoCo LOADM/CLOADM byte stream - see ../../src/fileformat/decb/DecbFormat.h.
# Both machines' ROM BASIC read this same block structure from disk or cassette, and XRoar (the
# emulator that covers both machines) loads it directly either way.
$(BUILD_DIR)/main.decb: $(BUILD_DIR)/main.o $(DRAGON32_LIB)
	$(RCC) --link --platform=6809-decb --out="$@" "$(BUILD_DIR)/main.o" "$(DRAGON32_LIB)"

# Second build/link check, exercising every dragon32.h function rather than just main.c's
# hello-world subset - see api_test.c's own header comment.
$(BUILD_DIR)/api_test.raw: $(BUILD_DIR)/api_test.o $(DRAGON32_LIB)
	$(RCC) --link --platform=6809-raw --out="$@" "$(BUILD_DIR)/api_test.o" "$(DRAGON32_LIB)"

# No Python (or other) 6809 emulator is wired into this project anywhere yet - see
# docs/known-bugs.md - so this only confirms every output exists.
test: all
	test -s "$(BUILD_DIR)/main.raw"
	test -s "$(BUILD_DIR)/main.decb"
	test -s "$(BUILD_DIR)/api_test.raw"

clean:
	rm -rf "$(BUILD_DIR)"
