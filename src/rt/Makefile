# Builds every runtime library under src/rt/ by delegating to each one's own
# Makefile - see apple2/Makefile, msdos/Makefile, trs80/Makefile, coleco/
# Makefile, coco/Makefile, dragon32/Makefile, dragon64/Makefile, vic20/Makefile, c64/Makefile,
# atari400/Makefile, atari5200/Makefile, oric/Makefile, bbc/Makefile, vex/Makefile, vectrex/Makefile,
# altair/Makefile, imsai/Makefile, intv/Makefile, msx/Makefile, sms/Makefile, and gamegear/Makefile.
SUBDIRS := apple2 msdos trs80 coleco coco dragon32 dragon64 vic20 c64 atari400 atari5200 oric bbc zxspectrum cpc zx81 ti994a vex vectrex altair imsai intv msx sms gamegear

.PHONY: all clean $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
