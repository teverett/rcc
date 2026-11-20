# Builds msx.c into msx.lib - see src/rt/trs80/Makefile's own header comment for what ".lib"
# means here (an ordinary --format=coff object, just named by convention - rcc has no archive
# format of its own).
RCC      ?= ../../../build/bin/rcc
TARGET   := msx.lib

$(TARGET): msx.c msx.h
	$(RCC) --target=z80-msx --format=coff --out=$(TARGET) msx.c

.PHONY: clean
clean:
	rm -f $(TARGET)
