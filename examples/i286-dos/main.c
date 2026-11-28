#include "../../src/rt/pcbios/pcbios.h"
#include "../../src/rt/msdos/msdos.h"

int main() {
    pcbios_clear_screen();
    pcbios_set_cursor(0, 0);
    pcbios_puts("HELLO FROM THE 80286!\r\n");
    pcbios_write_attr('!', 15, 3);

    // `pusha`/`popa` are new at the 80286 (see docs/platforms.md's own i286 entry
    // and src/codegen/x86_8086/I8086Assembler.cpp's `require286()`) - plain
    // --target=8086-dos would reject these two lines outright. They're a true
    // no-op pair here (every register pushed is popped straight back, nothing
    // in between touches the stack), so this only proves the i286 dialect's own
    // assembler accepts and correctly encodes a real 80286-only instruction
    // inside an ordinary C program, not just in tests/i286_assembler_test.cpp's
    // own isolated .asm text.
    asm {
        pusha
        popa
    }

    // A real program would loop on this to wait for a keypress; test.py runs
    // headlessly with nothing to type, so this only proves the call itself works.
    pcbios_haskey();

    dos_create("DONE.TXT", 0);
    pcbios_exit(0);
    return 0;
}
