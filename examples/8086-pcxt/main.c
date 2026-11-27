#include "../../src/rt/pcbios/pcbios.h"
#include "../../src/rt/msdos/msdos.h"

int main() {
    pcbios_clear_screen();
    pcbios_set_cursor(0, 0);
    pcbios_puts("HELLO FROM THE BIOS!\r\n");
    pcbios_write_attr('!', 15, 3);

    // A real program would loop on this to wait for a keypress; test.py runs
    // headlessly with nothing to type, so this only proves the call itself works.
    pcbios_haskey();

    dos_create("DONE.TXT", 0);
    pcbios_exit(0);
    return 0;
}
