#include "../../src/rt/vectrex/vectrex.h"

int main() {
    vectrex_intensity(0x7F);
    while (1) {
        vectrex_wait_recal();
        vectrex_dot(-20, -20);
        vectrex_dot(20, -20);
        vectrex_dot(20, 20);
        vectrex_dot(-20, 20);
    }
    return 0;
}
