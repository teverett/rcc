EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
APPLE2_DIR := $(EXAMPLE_DIR)../../src/rt/apple2
APPLE2_LIB := $(APPLE2_DIR)/apple2.lib
BASIC_APPLE2_LIB := $(APPLE2_DIR)/basic_apple2.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.prodos

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.bas | $(BUILD_DIR)
	$(RCC) --platform=6502-prodos --dump-ir --dump-asm --out="$@" "$<"

# apple2.lib/basic_apple2.lib are src/rt/apple2/'s own build products
# (see that directory's own Makefile) - built here via a sub-make rather
# than recompiling either a second time, so this example links the same
# objects every other apple2.c/basic_apple2.c consumer would.
$(APPLE2_LIB) $(BASIC_APPLE2_LIB):
	$(MAKE) -C $(APPLE2_DIR)

# --base=$2000, same reason as ../6502-dos33/Makefile's own: the default
# $0600 sits inside the Apple ]['s own text page 1 ($0400-$07FF), which
# this program's COUT/CROUT calls (via a2_cout()/a2_crout(), themselves
# called from basic_apple2.c's __basic_print_*) write through.
$(BUILD_DIR)/main.prodos: $(BUILD_DIR)/main.o $(APPLE2_LIB) $(BASIC_APPLE2_LIB)
	$(RCC) --link --platform=6502-prodos --base='$$2000' --out="$@" "$(BUILD_DIR)/main.o" "$(APPLE2_LIB)" "$(BASIC_APPLE2_LIB)"

# Same link, written out as `raw` instead of `prodos` - identical bytes
# minus the trailing file_type/aux_type footer - so CI can load and run
# it directly in py65 without stripping that footer back off first.
$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(APPLE2_LIB) $(BASIC_APPLE2_LIB)
	$(RCC) --link --platform=6502-raw --base='$$2000' --out="$@" "$(BUILD_DIR)/main.o" "$(APPLE2_LIB)" "$(BASIC_APPLE2_LIB)"

test: $(BUILD_DIR)/main.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"

clean:
	rm -rf "$(BUILD_DIR)"
