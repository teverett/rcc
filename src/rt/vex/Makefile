# Builds vex.c into vex.lib, a relocatable vex object - see src/rt/coco/Makefile's own comment
# for what "library" means here (there is no archive/`ar` format of rcc's own).
RCC      ?= ../../../build/bin/rcc
TARGET   := vex.lib

$(TARGET): vex.c vex.h
	$(RCC) --target=vex --format=coff --out=$(TARGET) vex.c

.PHONY: clean
clean:
	rm -f $(TARGET)
