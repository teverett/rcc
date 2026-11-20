# z80-gamegear

A Sega Game Gear example: writes the bytes `1, 2, 3, 4` into VRAM and one byte to the SN76489
sound chip, via `src/rt/gamegear/` - the same VDP/sound/controller port map as `../z80-sms/`
(see `src/rt/gamegear/gamegear.h`'s own header comment for the one real difference, the Start
button's own dedicated port).

See `../z80-sms/README.md` for why this writes a byte-exact pattern instead of readable text,
and for what `--base=0`/`--format=gg` do.

## Building

```
make                       # produces build/main.gg, a padded TMR SEGA cartridge image
make build/main.raw        # the same program, unpadded
```

## Why no real hardware to test against

Same situation as `../z80-sms/README.md` describes - CI loads `build/main.raw` into an
independent Z80 core (`z80-python`) with a small synthetic VDP/sound model standing in for the
real chips.
