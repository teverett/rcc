EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
ORIC_DIR := $(EXAMPLE_DIR)../../src/rt/oric
ORIC_LIB := $(ORIC_DIR)/oric.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.raw $(BUILD_DIR)/api_test.raw

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --platform=6502-raw --dump-ir --dump-asm --out="$@" "$<"

$(BUILD_DIR)/api_test.o: api_test.c | $(BUILD_DIR)
	$(RCC) --platform=6502-raw --dump-ir --dump-asm --out="$@" "$<"

# oric.lib is src/rt/oric/'s own build product (see that directory's own Makefile) - built here
# via a sub-make rather than recompiling oric.c a second time, the same pattern
# examples/6502-vic20/Makefile uses for vic20.lib.
$(ORIC_LIB):
	$(MAKE) -C $(ORIC_DIR)

$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(ORIC_LIB)
	$(RCC) --link --platform=6502-raw --out="$@" "$(BUILD_DIR)/main.o" "$(ORIC_LIB)"

# Second build/link/run check, exercising every oric.h function rather than just main.c's
# hello-world subset - see api_test.c's own header comment.
$(BUILD_DIR)/api_test.raw: $(BUILD_DIR)/api_test.o $(ORIC_LIB)
	$(RCC) --link --platform=6502-raw --out="$@" "$(BUILD_DIR)/api_test.o" "$(ORIC_LIB)"

# Runs both main.raw and api_test.raw in py65's 6502 MPU core - see each script's own header
# comment.
test: $(BUILD_DIR)/main.raw $(BUILD_DIR)/api_test.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"
	python3 "api_test.py" "$(BUILD_DIR)/api_test.raw"

clean:
	rm -rf "$(BUILD_DIR)"
