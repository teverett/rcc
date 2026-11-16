# Builds test1.c for --format=raw compile-only platforms too (so a
# codegen regression there still shows up as a build failure), and
# runs it under every target's real hardware simulator this repo has
# wired up, checking its single returned int against test1.c's own
# "// EXPECT:" comment.
#
# Targets excluded from TEST_TARGETS, and why (see ../../docs/known-bugs.md
# and ../../tests/run_tests.sh's own header comment):
#   6809, 6809-vectrex, tms9900, z8000, z80-coleco - no verified simulator exists;
#   compile-only, see the `compile-only` target below.
#   8086, 8088                              - run_tests.sh has no working
#   test execution for these (no boot-sector mechanism).
#   stm8, m16c, m30626fjpfp, sparc, pic16, pic18, moarvm, pdp1-as-a-standalone-exe
#     - no run_tests.sh check function at all.
#
# Every target below is one tests/run_tests.sh already knows how to
# run in a simulator and check (see ../../tests/run_tests.sh); this
# Makefile exercises test1.c specifically, through run_on_target.sh,
# which reuses that script's own per-target check_<target>() functions.

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT := $(HERE)../..
RCC ?= $(ROOT)/build/bin/rcc

TEST_TARGETS := \
	pdp1 pdp7 pdp11 pdp10 vax avr alpha saturn tms320c6713 \
	r10000 powerpc cortex-m3 \
	6502 65c02 68000 mc68328 8080 z80 8008 8051 stm32 i386 \
	armv7a armv8a vex

COMPILE_ONLY_TARGETS := 6809 6809-vectrex tms9900 z8000

.PHONY: all test $(TEST_TARGETS) compile-only clean

all: test

$(RCC):
	$(MAKE) -C $(ROOT)

# Runs test1.c through every target's simulator; stops at the first
# target that gives the wrong answer (or won't compile/run at all).
test: $(RCC)
	@status=0; \
	for t in $(TEST_TARGETS); do \
		$(HERE)run_on_target.sh "$$t" "$(RCC)" || status=1; \
	done; \
	exit $$status

# One target at a time, e.g. `make 6502`.
$(TEST_TARGETS): %: $(RCC)
	$(HERE)run_on_target.sh "$@" "$(RCC)"

# Targets with no verified simulator (see the header comment above):
# confirms test1.c still compiles there, without claiming to have run it.
compile-only: $(RCC)
	@status=0; \
	for t in $(COMPILE_ONLY_TARGETS); do \
		if $(RCC) --target="$$t" --format=raw --out="$(HERE)build/test1-$$t.bin" "$(HERE)test1.c"; then \
			echo "COMPILE-OK test1.c ($$t)"; \
		else \
			echo "FAIL test1.c: compile error ($$t)"; \
			status=1; \
		fi; \
	done; \
	exit $$status

clean:
	rm -rf $(HERE)build
