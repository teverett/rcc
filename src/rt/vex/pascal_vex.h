#ifndef RCC_RT_PASCAL_VEX_H
#define RCC_RT_PASCAL_VEX_H

// Vex implementation of the runtime calls src/frontend/pascal/
// PascalFrontend.cpp's write/writeln lowering emits (see that file's
// own header comment and docs/pascal-language.md) - the Pascal-
// frontend analog of vex.c/vex.h, built on top of them
// (vex_putc()/vex_puts()) rather than touching ports directly.
//
// Only write/writeln's own calls are implemented here - no read/readln
// yet; see src/rt/apple2/pascal_apple2.h's own comment for why a
// Pascal program that only writes needs just this file linked
// alongside vex.lib, not those two names at all.

void __pascal_write_int(int v);
void __pascal_write_char(int c);
void __pascal_write_str(char *s);
void __pascal_write_newline();

#endif
