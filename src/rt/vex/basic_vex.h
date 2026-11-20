#ifndef RCC_RT_BASIC_VEX_H
#define RCC_RT_BASIC_VEX_H

// Vex implementation of the runtime calls src/frontend/basic/
// BasicFrontend.cpp's PRINT lowering emits (see that file's own header
// comment and docs/basic-language.md) - the BASIC-frontend analog of
// vex.c/vex.h, built on top of them (vex_putc()/vex_puts()) rather than
// touching ports directly.
//
// Only PRINT's own calls are implemented here - no INPUT (__basic_
// input_int/__basic_input_str) yet; see src/rt/apple2/basic_apple2.h's
// own comment for why a BASIC program that only PRINTs needs just this
// file linked alongside vex.lib, not those two names at all.

void __basic_print_int(int v);
void __basic_print_str(char *s);
void __basic_print_tab();
void __basic_print_newline();

#endif
