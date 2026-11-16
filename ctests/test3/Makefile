# Same shape as ../test1/Makefile and ../test2/Makefile: builds+runs
# test3.c under every target's own real hardware simulator, checking
# its single returned int against test3.c's own "// EXPECT:" comment,
# by reusing ../../tests/run_tests.sh's own check_<target>()
# functions through run_on_target.sh.
#
# test3.c uses no struct and no pointer-to-struct, so it isn't hit by
# either of ../test2/Makefile's own exclusions (the 8051 global-struct
# XDATA sizing bug, or vex's lack of struct support at all - see
# ../../docs/known-bugs.md and ../test2/Makefile's header comment) -
# this uses the full target list from ../test1/Makefile.

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT := $(HERE)../..
RCC ?= $(ROOT)/build/bin/rcc

TEST_TARGETS := \
	pdp1 pdp7 pdp11 pdp10 vax avr alpha saturn tms320c6713 \
	r10000 powerpc cortex-m3 \
	6502 65c02 68000 mc68328 8080 z80 8008 8051 stm32 i386 \
	armv7a armv8a vex

COMPILE_ONLY_TARGETS := 6809 6809-vectrex tms9900

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
		if $(RCC) --target="$$t" --format=raw --out="$(HERE)build/test3-$$t.bin" "$(HERE)test3.c"; then \
			echo "COMPILE-OK test3.c ($$t)"; \
		else \
			echo "FAIL test3.c: compile error ($$t)"; \
			status=1; \
		fi; \
	done; \
	exit $$status

clean:
	rm -rf $(HERE)build
