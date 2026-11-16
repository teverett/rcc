# Builds altair.c into altair.lib - see src/rt/trs80/Makefile's own header
# comment for what ".lib" means here (an ordinary --format=coff object,
# just named by convention - rcc has no archive format of its own).
RCC      ?= ../../../build/bin/rcc
TARGET   := altair.lib

$(TARGET): altair.c altair.h
	$(RCC) --target=8080 --format=coff --out=$(TARGET) altair.c

.PHONY: clean
clean:
	rm -f $(TARGET)
