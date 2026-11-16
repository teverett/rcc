# Same shape as ../test1/Makefile: builds+runs test2.c under every
# target's own real hardware simulator, checking its single returned
# int against test2.c's own "// EXPECT:" comment, by reusing
# ../../tests/run_tests.sh's own check_<target>() functions through
# run_on_target.sh.
#
# Targets excluded from TEST_TARGETS, beyond ../test1/Makefile's own
# list of exclusions (no simulator at all / disabled execution - see
# that file's header comment and ../../docs/known-bugs.md):
#
#   vex   - this backend doesn't support `struct` at all yet
#           (src/codegen/vex/VexCodegen.cpp fails any struct-typed
#           local/global with "struct isn't supported on this
#           backend yet"), and test2.c uses one.
#
# 8051 was excluded here for the same reason test2.c uses a global
# struct at all: a since-fixed MCS51Codegen.cpp bug reserved only 2
# bytes of XDATA for any non-array global, silently corrupting a
# wider global struct (see tests/struct_global_sizing.c and
# tests/struct_global_with_local_and_pointer.c for the regression
# tests that cover it now).

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT := $(HERE)../..
RCC ?= $(ROOT)/build/bin/rcc

TEST_TARGETS := \
	pdp1 pdp7 pdp11 pdp10 vax avr alpha saturn tms320c6713 \
	r10000 powerpc cortex-m3 \
	6502 65c02 68000 mc68328 8080 z80 8008 8051 stm32 i386 \
	armv7a armv8a

COMPILE_ONLY_TARGETS := 6809 6809-vectrex tms9900 z8000

.PHONY: all test $(TEST_TARGETS) compile-only clean

all: test

$(RCC):
	$(MAKE) -C $(ROOT)

test: $(RCC)
	@status=0; \
	for t in $(TEST_TARGETS); do \
		$(HERE)run_on_target.sh "$$t" "$(RCC)" || status=1; \
	done; \
	exit $$status

$(TEST_TARGETS): %: $(RCC)
	$(HERE)run_on_target.sh "$@" "$(RCC)"

compile-only: $(RCC)
	@status=0; \
	for t in $(COMPILE_ONLY_TARGETS); do \
		if $(RCC) --target="$$t" --format=raw --out="$(HERE)build/test2-$$t.bin" "$(HERE)test2.c"; then \
			echo "COMPILE-OK test2.c ($$t)"; \
		else \
			echo "FAIL test2.c: compile error ($$t)"; \
			status=1; \
		fi; \
	done; \
	exit $$status

clean:
	rm -rf $(HERE)build
