# Builds intv.c into intv.lib, a relocatable CP1610 COFF object - see src/rt/coco/Makefile's own
# header comment for why "library" here just means --format=coff's own ordinary object, renamed.
RCC      ?= ../../../build/bin/rcc
TARGET   := intv.lib

$(TARGET): intv.c intv.h
	$(RCC) --target=cp1610-intv --format=coff --out=$(TARGET) intv.c

.PHONY: clean
clean:
	rm -f $(TARGET)
