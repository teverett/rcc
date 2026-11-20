# Builds vex.c into vex.lib, a relocatable vex object - see src/rt/coco/Makefile's own comment
# for what "library" means here (there is no archive/`ar` format of rcc's own).
RCC      ?= ../../../build/bin/rcc
TARGET   := vex.lib
BASIC_TARGET := basic_vex.lib

all: $(TARGET) $(BASIC_TARGET)

$(TARGET): vex.c vex.h
	$(RCC) --target=vex --format=coff --out=$(TARGET) vex.c

# The BASIC frontend's own runtime calls (see basic_vex.h) - built on top
# of vex.c's vex_putc()/vex_puts(), but compiled and linked as its own
# object so a BASIC program only has to link it alongside vex.lib;
# nothing else here needs it.
$(BASIC_TARGET): basic_vex.c basic_vex.h vex.h
	$(RCC) --target=vex --format=coff --out=$(BASIC_TARGET) basic_vex.c

.PHONY: all clean
clean:
	rm -f $(TARGET) $(BASIC_TARGET)
