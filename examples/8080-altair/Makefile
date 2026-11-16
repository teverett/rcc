EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
ALTAIR_DIR := $(EXAMPLE_DIR)../../src/rt/altair
ALTAIR_LIB := $(ALTAIR_DIR)/altair.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.raw

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --platform=8080-raw --dump-ir --dump-asm --out="$@" "$<"

# altair.lib is src/rt/altair/'s own build product (see that directory's own
# Makefile) - built here via a sub-make rather than recompiling altair.c a
# second time, so this example links the same object every other altair.c
# consumer would.
$(ALTAIR_LIB):
	$(MAKE) -C $(ALTAIR_DIR)

$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(ALTAIR_LIB)
	$(RCC) --link --platform=8080-raw --out="$@" "$(BUILD_DIR)/main.o" "$(ALTAIR_LIB)"

# Runs main.raw on a real, independent 8080-compatible core (test.py) - see
# that script's own header comment.
test: $(BUILD_DIR)/main.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"

clean:
	rm -rf "$(BUILD_DIR)"
