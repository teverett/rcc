# z80-sms

A Sega Master System example: writes the bytes `1, 2, 3, 4` into VRAM and one byte to the
SN76489 sound chip, via `src/rt/sms/` - a runtime library that reaches the VDP, sound chip, and
controller ports entirely through the Z80's `IN`/`OUT` instructions, the same shape
`../z80-coleco/` already uses for the ColecoVision.

There's no BIOS-supplied font or established VDP mode a cartridge here can rely on (see
`src/rt/sms/sms.h`'s own header comment - a cartridge boots straight into user code, unlike
ColecoVision's own BIOS-preloaded font), so this is the closest thing to a "hello world" that's
honest about that limit, the same approach `../6502-atari2600/`'s own example takes.

## Building

```
make                       # produces build/main.sms, a padded TMR SEGA cartridge image
make build/main.raw        # the same program, unpadded (no cartridge header/checksum)
```

`--base=0`: the SMS cartridge ROM window starts at `$0000`, not plain `z80`'s own default
`$8000` base (see `src/fileformat/smsrom/SmsRomFormat.h`'s own header comment, and its writer's
own base-address check). `--format=sms` writes the real 16-byte "TMR SEGA" header (signature,
checksum, region/size byte - see that same header comment for the full layout); `--format=raw`
is the same bytes without it, used for this example's own CI check.

## Why no real hardware to test against

Like `../z80-coleco/`, there's no SMS BIOS/VDP reference bundled with this project to confirm
every detail byte-for-byte. CI instead loads `build/main.raw` into an independent Z80 core
(`z80-python`) with a small synthetic VDP/sound model standing in for the real chips - enough to
observe the exact bytes this program writes through real `OUT`/`IN` instructions, not a
cycle-accurate 315-5124.
