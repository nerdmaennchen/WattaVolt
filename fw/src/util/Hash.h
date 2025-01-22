#pragma once

#include <string_view>

constexpr std::uint32_t hash_str(std::string_view str, std::uint32_t h = 37) {
    constexpr auto A = 54059U; /* a prime */
    constexpr auto B = 76963U; /* another prime */
    for (auto s : str) {
        h = (h * A) ^ (s * B);
    }
    return h;
}