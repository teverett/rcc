# z80-msx

An MSX "hello world": prints `HELLO, WORLD!` via `src/rt/msx/`, a runtime library that reaches
the MSX BIOS through documented `CALL` entry points (CHPUT/CHGET/GTSTCK/WRTVDP/WRTVRM/RDVRM/
WRTPSG/INITXT - see `src/rt/msx/msx.h`'s own header comment) rather than touching any hardware
register directly - a cartridge's own code runs with the BIOS still mapped, unlike a ZX
Spectrum `.sna` snapshot (`../z80-zxspectrum/`) which boots straight past it.

## Building

```
make                       # produces build/main.msx, a padded cartridge image
make build/main.raw        # the same program, unpadded (no standard-size rounding)
```

`--platform=z80-msx-msxrom` links against an MSX-specific entry object: the cartridge header (an
`"AB"` signature and an INIT vector the BIOS reads during its own cartridge scan before ever
calling into this program's own code - see `src/linker/factory/MsxLinkerFactory.cpp`'s own
header comment) instead of a plain `call fn_main; halt`.

Its `msxrom` format pads the linked image with `$FF` up to the nearest standard cartridge size
(8/16/32 KiB - see `src/fileformat/msxrom/MsxRomFormat.h`); `--platform=z80-msx-raw` is the same
bytes without that padding, used for this example's own CI check (and by
`tests/run_msx_rt_tests.sh`).

## Why no real BIOS to test against

There's no MSX BIOS ROM dump bundled with this project (shipping one would mean bundling
copyrighted firmware). CI instead loads `build/main.raw` into an independent Z80 core
(`z80-python`), patches each BIOS entry point under test with a bare `RET`, and checks that this
program's own code sets up the documented calling convention (which register holds what) and
calls the documented address, in the documented order - not that a real BIOS then does the right
thing with it, which there's no ROM here to check.
