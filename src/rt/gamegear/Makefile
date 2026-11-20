# Builds gamegear.c into gamegear.lib - see src/rt/trs80/Makefile's own header comment for what
# ".lib" means here. --base=0 at link time, not here - see sms/Makefile's own header comment.
RCC      ?= ../../../build/bin/rcc
TARGET   := gamegear.lib

$(TARGET): gamegear.c gamegear.h
	$(RCC) --target=z80 --format=coff --out=$(TARGET) gamegear.c

.PHONY: clean
clean:
	rm -f $(TARGET)
