EXAMPLE_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= build
SMS_DIR := $(EXAMPLE_DIR)../../src/rt/sms
SMS_LIB := $(SMS_DIR)/sms.lib

include $(EXAMPLE_DIR)../examples.mk

.PHONY: all test clean

all: $(BUILD_DIR)/main.sms

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(RCC) --debug-map --target=z80 --format=coff --dump-ir --dump-asm --out="$@" "$<"

# sms.lib is src/rt/sms/'s own build product (see that directory's own Makefile) - built here
# via a sub-make rather than recompiling sms.c a second time.
$(SMS_LIB):
	$(MAKE) -C $(SMS_DIR)

$(BUILD_DIR)/main.sms: $(BUILD_DIR)/main.o $(SMS_LIB)
	$(RCC) --debug-map --link --target=z80 --base=0 --format=sms --out="$@" "$(BUILD_DIR)/main.o" "$(SMS_LIB)"

# Same link, written out as `raw` instead of `sms` - identical bytes minus the TMR SEGA header/
# padding (see ../../src/fileformat/smsrom/SmsRomFormat.h) - so CI can load and run it directly
# in a Z80 simulator without padding to worry about.
$(BUILD_DIR)/main.raw: $(BUILD_DIR)/main.o $(SMS_LIB)
	$(RCC) --link --target=z80 --base=0 --format=raw --out="$@" "$(BUILD_DIR)/main.o" "$(SMS_LIB)"

# Runs main.raw on a real Z80 core (test.py) - see that script's own header comment.
test: $(BUILD_DIR)/main.raw
	python3 "test.py" "$(BUILD_DIR)/main.raw"

clean:
	rm -rf "$(BUILD_DIR)"
