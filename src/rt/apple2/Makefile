# Builds apple2.c into apple2.lib, a relocatable 6502 COFF object - rcc has
# no archive/`ar` format of its own (ArchiveReader.h only *reads* archives,
# for hosted-target `-l<name>` lookup; see docs/using.md), so "library" here
# means exactly what --format=coff already produces for any object-format
# compile: one ordinary object this directory just names with a .lib
# extension by convention. `--link` takes it as a plain positional object,
# the same as any other .o - see docs/using.md's "Link mode".
RCC      ?= ../../../build/bin/rcc
TARGET   := apple2.lib
BASIC_TARGET := basic_apple2.lib
PASCAL_TARGET := pascal_apple2.lib

all: $(TARGET) $(BASIC_TARGET) $(PASCAL_TARGET)

$(TARGET): apple2.c apple2.h
	$(RCC) --target=6502 --format=coff --out=$(TARGET) apple2.c

# The BASIC frontend's own runtime calls (see basic_apple2.h) - built on
# top of apple2.c's a2_cout()/a2_puts()/a2_crout(), but compiled and
# linked as its own object so a BASIC program only has to link it
# alongside apple2.lib; nothing else here needs it.
$(BASIC_TARGET): basic_apple2.c basic_apple2.h apple2.h
	$(RCC) --target=6502 --format=coff --out=$(BASIC_TARGET) basic_apple2.c

# The Pascal frontend's own runtime calls (see pascal_apple2.h) - same
# shape as $(BASIC_TARGET) above, built on top of apple2.c and linked
# as its own object so a Pascal program only has to link it alongside
# apple2.lib.
$(PASCAL_TARGET): pascal_apple2.c pascal_apple2.h apple2.h
	$(RCC) --target=6502 --format=coff --out=$(PASCAL_TARGET) pascal_apple2.c

.PHONY: all clean
clean:
	rm -f $(TARGET) $(BASIC_TARGET) $(PASCAL_TARGET)
