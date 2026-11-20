#ifndef RCC_RT_PASCAL_APPLE2_H
#define RCC_RT_PASCAL_APPLE2_H

// Apple ][ implementation of the runtime calls src/frontend/pascal/
// PascalFrontend.cpp's write/writeln lowering emits (see that file's
// own header comment and docs/pascal-language.md) - the Pascal-
// frontend analog of apple2.c/apple2.h, built on top of them
// (a2_cout()/a2_puts()/a2_crout()) rather than calling the monitor ROM
// directly.
//
// Only write/writeln's own calls are implemented here - no read/readln
// (__pascal_read_int/__pascal_read_char) yet, same deferral
// basic_apple2.h's own header comment already made for BASIC's INPUT
// (GETLN's line buffer isn't read back into a value yet); a Pascal
// program that only writes needs just this file linked alongside
// apple2.lib, not those two names at all - see PascalFrontend.cpp's
// own ensureExternFn (a runtime call only gets declared, and so only
// needs to resolve, when the construct that needs it actually appears
// in the source).

void __pascal_write_int(int v);
void __pascal_write_char(int c);
void __pascal_write_str(char *s);
void __pascal_write_newline();

#endif
