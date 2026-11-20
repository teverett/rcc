# Builds oric.c into oric.lib, a relocatable 6502 COFF object - rcc has no archive/`ar` format of
# its own (ArchiveReader.h only *reads* archives, for hosted-target `-l<name>` lookup; see
# docs/using.md), so "library" here means exactly what --format=coff already produces for any
# object-format compile: one ordinary object this directory just names with a .lib extension by
# convention. `--link` takes it as a plain positional object, the same as any other .o - see
# docs/using.md's "Link mode".
RCC      ?= ../../../build/bin/rcc
TARGET   := oric.lib

$(TARGET): oric.c oric.h
	$(RCC) --target=6502 --format=coff --out=$(TARGET) oric.c

.PHONY: clean
clean:
	rm -f $(TARGET)
