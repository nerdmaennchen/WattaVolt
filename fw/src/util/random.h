#pragma once

#include <cstdint>
#include <limits>

std::uint8_t random_byte();

struct RP2040_rand_dev {
    RP2040_rand_dev() = default;
    
    RP2040_rand_dev& operator=(RP2040_rand_dev const&) = delete;

    std::uint32_t operator()() {
        return (random_byte() << 24) | 
                (random_byte() << 16) |
                (random_byte() << 8) |
                (random_byte() << 0);
    }

    static std::uint32_t min() {
        return 0;
    }
    static std::uint32_t max() {
        return std::numeric_limits<std::uint32_t>::max();
    }
};