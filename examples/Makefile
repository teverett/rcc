EXAMPLES_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all hosted-hello m30626fjpfp-freestanding 6502-freestanding 68000-freestanding pdp7-freestanding 6502-dos33 6502-vic20 6502-c64 6502-atari400 6502-bbc 6502-atari2600 6502-2a03 6502-5200 6502-oric 8086-msdos 8086-pcxt z80-trs80 z80-coleco z80-zxspectrum z80-cpc z80-zx81 z80-msx z80-sms z80-gamegear 8080-altair 8080-imsai 6809-coco 6809-dragon32 6809-dragon64 6809-vectrex cp1610-intv tms9900-ti994a vex moarvm-hello test clean

all: hosted-hello m30626fjpfp-freestanding 6502-freestanding 68000-freestanding pdp7-freestanding 6502-dos33 6502-vic20 6502-c64 6502-atari400 6502-bbc 6502-atari2600 6502-2a03 6502-5200 6502-oric 8086-msdos 8086-pcxt z80-trs80 z80-coleco z80-zxspectrum z80-cpc z80-zx81 z80-msx z80-sms z80-gamegear 8080-altair 8080-imsai 6809-coco 6809-dragon32 6809-dragon64 6809-vectrex cp1610-intv tms9900-ti994a vex moarvm-hello

hosted-hello:
	$(MAKE) -C $(EXAMPLES_DIR)hosted-hello

m30626fjpfp-freestanding:
	$(MAKE) -C $(EXAMPLES_DIR)m30626fjpfp-freestanding

6502-freestanding:
	$(MAKE) -C $(EXAMPLES_DIR)6502-freestanding

68000-freestanding:
	$(MAKE) -C $(EXAMPLES_DIR)68000-freestanding

pdp7-freestanding:
	$(MAKE) -C $(EXAMPLES_DIR)pdp7-freestanding

6502-dos33:
	$(MAKE) -C $(EXAMPLES_DIR)6502-dos33

6502-vic20:
	$(MAKE) -C $(EXAMPLES_DIR)6502-vic20

6502-c64:
	$(MAKE) -C $(EXAMPLES_DIR)6502-c64

6502-atari400:
	$(MAKE) -C $(EXAMPLES_DIR)6502-atari400

6502-bbc:
	$(MAKE) -C $(EXAMPLES_DIR)6502-bbc

6502-atari2600:
	$(MAKE) -C $(EXAMPLES_DIR)6502-atari2600

6502-2a03:
	$(MAKE) -C $(EXAMPLES_DIR)6502-2a03

6502-5200:
	$(MAKE) -C $(EXAMPLES_DIR)6502-5200

6502-oric:
	$(MAKE) -C $(EXAMPLES_DIR)6502-oric

8086-msdos:
	$(MAKE) -C $(EXAMPLES_DIR)8086-msdos

8086-pcxt:
	$(MAKE) -C $(EXAMPLES_DIR)8086-pcxt

z80-trs80:
	$(MAKE) -C $(EXAMPLES_DIR)z80-trs80

z80-coleco:
	$(MAKE) -C $(EXAMPLES_DIR)z80-coleco

z80-zxspectrum:
	$(MAKE) -C $(EXAMPLES_DIR)z80-zxspectrum

z80-cpc:
	$(MAKE) -C $(EXAMPLES_DIR)z80-cpc

z80-zx81:
	$(MAKE) -C $(EXAMPLES_DIR)z80-zx81

z80-msx:
	$(MAKE) -C $(EXAMPLES_DIR)z80-msx

z80-sms:
	$(MAKE) -C $(EXAMPLES_DIR)z80-sms

z80-gamegear:
	$(MAKE) -C $(EXAMPLES_DIR)z80-gamegear

8080-altair:
	$(MAKE) -C $(EXAMPLES_DIR)8080-altair

8080-imsai:
	$(MAKE) -C $(EXAMPLES_DIR)8080-imsai

6809-coco:
	$(MAKE) -C $(EXAMPLES_DIR)6809-coco

6809-dragon32:
	$(MAKE) -C $(EXAMPLES_DIR)6809-dragon32

6809-dragon64:
	$(MAKE) -C $(EXAMPLES_DIR)6809-dragon64

6809-vectrex:
	$(MAKE) -C $(EXAMPLES_DIR)6809-vectrex

cp1610-intv:
	$(MAKE) -C $(EXAMPLES_DIR)cp1610-intv

tms9900-ti994a:
	$(MAKE) -C $(EXAMPLES_DIR)tms9900-ti994a

vex:
	$(MAKE) -C $(EXAMPLES_DIR)vex

moarvm-hello:
	$(MAKE) -C $(EXAMPLES_DIR)moarvm-hello

# Each example's own `test` target builds it, then verifies it in
# whichever simulator (py65, z80-python, qemu-system-m68k) that target
# has one wired up for - see each example's own Makefile/README for which.
test:
	$(MAKE) -C $(EXAMPLES_DIR)hosted-hello test
	$(MAKE) -C $(EXAMPLES_DIR)m30626fjpfp-freestanding test
	$(MAKE) -C $(EXAMPLES_DIR)6502-freestanding test
	$(MAKE) -C $(EXAMPLES_DIR)68000-freestanding test
	$(MAKE) -C $(EXAMPLES_DIR)pdp7-freestanding test
	$(MAKE) -C $(EXAMPLES_DIR)6502-dos33 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-vic20 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-c64 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-atari400 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-bbc test
	$(MAKE) -C $(EXAMPLES_DIR)6502-atari2600 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-2a03 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-5200 test
	$(MAKE) -C $(EXAMPLES_DIR)6502-oric test
	$(MAKE) -C $(EXAMPLES_DIR)z80-trs80 test
	$(MAKE) -C $(EXAMPLES_DIR)z80-coleco test
	$(MAKE) -C $(EXAMPLES_DIR)z80-zxspectrum test
	$(MAKE) -C $(EXAMPLES_DIR)z80-cpc test
	$(MAKE) -C $(EXAMPLES_DIR)z80-zx81 test
	$(MAKE) -C $(EXAMPLES_DIR)z80-msx test
	$(MAKE) -C $(EXAMPLES_DIR)z80-sms test
	$(MAKE) -C $(EXAMPLES_DIR)z80-gamegear test
	$(MAKE) -C $(EXAMPLES_DIR)8080-altair test
	$(MAKE) -C $(EXAMPLES_DIR)8080-imsai test
	$(MAKE) -C $(EXAMPLES_DIR)8086-msdos test
	$(MAKE) -C $(EXAMPLES_DIR)8086-pcxt test
	$(MAKE) -C $(EXAMPLES_DIR)6809-coco test
	$(MAKE) -C $(EXAMPLES_DIR)6809-dragon32 test
	$(MAKE) -C $(EXAMPLES_DIR)6809-dragon64 test
	$(MAKE) -C $(EXAMPLES_DIR)6809-vectrex test
	$(MAKE) -C $(EXAMPLES_DIR)cp1610-intv test
	$(MAKE) -C $(EXAMPLES_DIR)tms9900-ti994a test
	$(MAKE) -C $(EXAMPLES_DIR)vex test
	$(MAKE) -C $(EXAMPLES_DIR)moarvm-hello test

clean:
	$(MAKE) -C $(EXAMPLES_DIR)hosted-hello clean
	$(MAKE) -C $(EXAMPLES_DIR)m30626fjpfp-freestanding clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-freestanding clean
	$(MAKE) -C $(EXAMPLES_DIR)68000-freestanding clean
	$(MAKE) -C $(EXAMPLES_DIR)pdp7-freestanding clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-dos33 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-vic20 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-c64 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-atari400 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-bbc clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-atari2600 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-2a03 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-5200 clean
	$(MAKE) -C $(EXAMPLES_DIR)6502-oric clean
	$(MAKE) -C $(EXAMPLES_DIR)8086-msdos clean
	$(MAKE) -C $(EXAMPLES_DIR)8086-pcxt clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-trs80 clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-coleco clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-zxspectrum clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-cpc clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-zx81 clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-msx clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-sms clean
	$(MAKE) -C $(EXAMPLES_DIR)z80-gamegear clean
	$(MAKE) -C $(EXAMPLES_DIR)8080-altair clean
	$(MAKE) -C $(EXAMPLES_DIR)8080-imsai clean
	$(MAKE) -C $(EXAMPLES_DIR)6809-coco clean
	$(MAKE) -C $(EXAMPLES_DIR)6809-dragon32 clean
	$(MAKE) -C $(EXAMPLES_DIR)6809-dragon64 clean
	$(MAKE) -C $(EXAMPLES_DIR)6809-vectrex clean
	$(MAKE) -C $(EXAMPLES_DIR)cp1610-intv clean
	$(MAKE) -C $(EXAMPLES_DIR)tms9900-ti994a clean
	$(MAKE) -C $(EXAMPLES_DIR)vex clean
	$(MAKE) -C $(EXAMPLES_DIR)moarvm-hello clean
