#ifndef RCC_RT_BASIC_APPLE2_H
#define RCC_RT_BASIC_APPLE2_H

// Apple ][ implementation of the runtime calls src/frontend/basic/
// BasicFrontend.cpp's PRINT lowering emits (see that file's own header
// comment and docs/basic-language.md) - the BASIC-frontend analog of
// apple2.c/apple2.h, built on top of them (a2_cout()/a2_puts()/
// a2_crout()) rather than calling the monitor ROM directly.
//
// Only PRINT's own calls are implemented here - no INPUT (__basic_
// input_int/__basic_input_str) yet, since reading GETLN's buffer back
// into a value hasn't been written; a BASIC program that only PRINTs
// needs just this file linked alongside apple2.lib, not those two names
// at all (see BasicFrontend.cpp's own ensureExternFn - a runtime call
// only gets declared, and so only needs to resolve, when the construct
// that needs it actually appears in the source).

void __basic_print_int(int v);
void __basic_print_str(char *s);
void __basic_print_tab();
void __basic_print_newline();

#endif
