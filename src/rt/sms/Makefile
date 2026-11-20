# Builds sms.c into sms.lib - see src/rt/trs80/Makefile's own header comment for what ".lib"
# means here (an ordinary --format=coff object, just named by convention - rcc has no archive
# format of its own). --base=0: the SMS cartridge ROM window starts at $0000, not plain z80's
# own default $8000 base (see sms.h's own header comment, and --format=sms's own base-address
# check in SmsRomFormat.cpp).
RCC      ?= ../../../build/bin/rcc
TARGET   := sms.lib

$(TARGET): sms.c sms.h
	$(RCC) --target=z80 --format=coff --out=$(TARGET) sms.c

.PHONY: clean
clean:
	rm -f $(TARGET)
