# Builds atari5200.c into atari5200.lib, a relocatable 6502 COFF object - see
# ../atari400/Makefile's own header comment for why "library" here just means an ordinary
# --format=coff object with a .lib extension by convention.
RCC      ?= ../../../build/bin/rcc
TARGET   := atari5200.lib

$(TARGET): atari5200.c atari5200.h
	$(RCC) --target=5200 --format=coff --out=$(TARGET) atari5200.c

.PHONY: clean
clean:
	rm -f $(TARGET)
