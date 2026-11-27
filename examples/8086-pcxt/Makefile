EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(EXAMPLE_DIR)build
PCBIOS_DIR := $(EXAMPLE_DIR)../../src/rt/pcbios
PCBIOS_LIB := $(PCBIOS_DIR)/pcbios.lib
MSDOS_DIR := $(EXAMPLE_DIR)../../src/rt/msdos
MSDOS_LIB := $(MSDOS_DIR)/msdos.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.com

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --debug-map --platform=8086-dos-raw --out="$@" "$<"

# pcbios.lib/msdos.lib are src/rt/pcbios/'s and src/rt/msdos/'s own build products
# (built here via a sub-make rather than recompiling either a second time).
$(PCBIOS_LIB):
	$(MAKE) -C $(PCBIOS_DIR)

$(MSDOS_LIB):
	$(MAKE) -C $(MSDOS_DIR)

$(BUILD_DIR)/main.com: $(BUILD_DIR)/main.o $(PCBIOS_LIB) $(MSDOS_LIB)
	$(RCC) --debug-map --link --platform=8086-dos-raw --out="$(BUILD_DIR)/main.com" "$(BUILD_DIR)/main.o" "$(PCBIOS_LIB)" "$(MSDOS_LIB)"

# Runs main.com headlessly under DOSBox-X (test.py) - see that script's own header
# comment. Skips the real run (compile/link only) if dosbox-x isn't installed, same as
# any other example whose emulator is optional on the local machine.
test: all
	@if command -v dosbox-x >/dev/null 2>&1; then \
		python3 "test.py" "$(BUILD_DIR)/main.com"; \
	else \
		echo "dosbox-x not found - skipping execution, build output exists" >&2; \
		test -s "$(BUILD_DIR)/main.com"; \
	fi

clean:
	rm -rf "$(BUILD_DIR)"
