#include "random.h"

#include <hardware/structs/rosc.h>

std::uint8_t random_byte() {
    std::uint8_t byte;
    for (int i =0; i < 8; ++i) {
        // picked a fairly arbitrary polynomial of 0x35u - this doesn't have to be crazily uniform.
        byte = ((byte << 1) | rosc_hw->randombit) ^ (byte & 0x80u ? 0x35u : 0);
        // delay a little because the random bit is a little slow
        busy_wait_at_least_cycles(30);
    }
    return byte;
}