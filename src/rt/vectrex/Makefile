# Builds vectrex.c into vectrex.lib, a relocatable 6809 COFF object - see
# src/rt/coco/Makefile's own comment for why ".lib" here just means "the
# object --link takes as a plain positional input", not a real archive.
RCC      ?= ../../../build/bin/rcc
TARGET   := vectrex.lib

$(TARGET): vectrex.c vectrex.h
	$(RCC) --target=6809-vectrex --format=coff --out=$(TARGET) vectrex.c

.PHONY: clean
clean:
	rm -f $(TARGET)
