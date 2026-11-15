EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.moarvm

$(BUILD_DIR):
	mkdir -p "$@"

# MoarVM is a whole-program target, like parrotvm/vex - there is no separate-compilation
# object step and no runtime library to link against (see docs/platforms.md's moarvm
# section), so a single rcc invocation produces the final .moarvm bytecode file directly.
$(BUILD_DIR)/main.moarvm: main.c | $(BUILD_DIR)
	$(RCC) --target=moarvm --dump-ir --dump-asm --out="$@" "$<"

# Runs main.moarvm in the real `moar` interpreter and checks it printed exactly the expected
# greeting. `moar` comes from `brew install moarvm` (or `nqp`, which pulls it in) - see
# .github/workflows/platformtests.yml for the CI install step.
test: $(BUILD_DIR)/main.moarvm
	@if ! command -v moar >/dev/null 2>&1; then \
		echo "SKIP: moar not installed (brew install moarvm)"; \
		exit 0; \
	fi
	@out="$$(moar "$(BUILD_DIR)/main.moarvm")"; \
	if [ "$$out" != "Hello, MoarVM!" ]; then \
		echo "FAIL: expected 'Hello, MoarVM!', got '$$out'"; \
		exit 1; \
	fi; \
	echo "PASS"

clean:
	rm -rf "$(BUILD_DIR)"
