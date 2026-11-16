# Aggregates every ctests/test*/ directory: each is self-contained
# (its own Makefile, its own test*.c, its own copy of
# run_on_target.sh), covering a different slice of rcc's supported C
# subset (see each one's own header comment) and compiled+run through
# every target with a working simulator.

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TESTS := test1 test2 test3

.PHONY: all test compile-only clean $(TESTS)

all: test

test:
	@status=0; \
	for t in $(TESTS); do \
		$(MAKE) -C $(HERE)$$t test || status=1; \
	done; \
	exit $$status

$(TESTS):
	$(MAKE) -C $(HERE)$@ test

compile-only:
	@status=0; \
	for t in $(TESTS); do \
		$(MAKE) -C $(HERE)$$t compile-only || status=1; \
	done; \
	exit $$status

clean:
	@for t in $(TESTS); do $(MAKE) -C $(HERE)$$t clean; done
