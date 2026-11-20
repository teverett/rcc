#include "../../src/rt/intv/intv.h"

int main() {
    intv_set_border_color(0);
    intv_set_color_stack(0, 1);
    intv_backtab_fill(0);

    // Pokes raw GROM card numbers for "HI" into the first two BACKTAB cells - see intv.h's own
    // intv_backtab_poke() comment for why this is a raw card number, not a full, color-stack-mode
    // BACKTAB word (this project couldn't verify that word's real bit layout from a primary
    // source).
    intv_backtab_poke(0, 0, INTV_GROM_CARD('H'));
    intv_backtab_poke(0, 1, INTV_GROM_CARD('I'));

    intv_enable_display();
    return 0;
}
