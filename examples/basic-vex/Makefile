EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT_DIR := $(EXAMPLE_DIR)../..
BUILD_DIR ?= build
VEX_RT_DIR := $(ROOT_DIR)/src/rt/vex
VEX_LIB := $(VEX_RT_DIR)/vex.lib
BASIC_VEX_LIB := $(VEX_RT_DIR)/basic_vex.lib
SIM_SRC := $(ROOT_DIR)/tests/sim/vex/vex.c
CC ?= cc

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.raw

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.bas | $(BUILD_DIR)
	$(RCC) --target=vex --format=coff --dump-ir --dump-asm --out="$@" "$<"

# vex.lib/basic_vex.lib are src/rt/vex/'s own build products (see that
# directory's own Makefile) - built here via a sub-make rather than
# recompiling either a second time, so this example links the same
# objects every other vex.c/basic_vex.c consumer would.
$(VEX_LIB) $(BASIC_VEX_LIB):
	$(MAKE) -C $(VEX_RT_DIR)

$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(VEX_LIB) $(BASIC_VEX_LIB)
	$(RCC) --link --platform=vex-raw --out="$@" "$(BUILD_DIR)/main.o" "$(VEX_LIB)" "$(BASIC_VEX_LIB)"

# The reference interpreter (vendored at tests/sim/vex/vex.c - see that file's own header
# comment) that runs main.raw for real.
$(BUILD_DIR)/vex_interp: $(SIM_SRC) | $(BUILD_DIR)
	$(CC) -O2 -o "$@" "$(SIM_SRC)"

# Runs main.raw in the real vex interpreter and checks it printed exactly the expected output -
# see test.py's own header comment for why PRINT/FOR/GOSUB all have to run correctly for this to
# come out right.
test: $(BUILD_DIR)/main.raw $(BUILD_DIR)/vex_interp
	@out="$$( "$(BUILD_DIR)/vex_interp" "$(BUILD_DIR)/main.raw" )"; \
	expected="$$(printf 'RCC BASIC ON VEX!\nSUM 1 TO 10 =\n55\nDOUBLED =\n110')"; \
	if [ "$$out" != "$$expected" ]; then \
		echo "FAIL: expected:"; echo "$$expected"; echo "got:"; echo "$$out"; \
		exit 1; \
	fi; \
	echo "PASS"

clean:
	rm -rf "$(BUILD_DIR)"
