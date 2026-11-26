# Builds imsai.c into imsai.lib - see src/rt/trs80/Makefile's own header
# comment for what ".lib" means here (an ordinary --format=coff object,
# just named by convention - rcc has no archive format of its own).
RCC      ?= ../../../build/bin/rcc
TARGET   := imsai.lib

$(TARGET): imsai.c imsai.h
	$(RCC) --target=8080 --format=coff --out=$(TARGET) imsai.c

.PHONY: clean
clean:
	rm -f $(TARGET)
