# Builds abios.c into abios.lib, a relocatable 8086-dos COFF object - see
# msdos/Makefile's own header comment for why "library" here just means
# --format=coff's ordinary output, named with a .lib extension by convention.
RCC      ?= ../../../build/bin/rcc
TARGET   := abios.lib

$(TARGET): abios.c abios.h
	$(RCC) --target=8086-dos --format=coff --out=$(TARGET) abios.c

.PHONY: clean
clean:
	rm -f $(TARGET)
