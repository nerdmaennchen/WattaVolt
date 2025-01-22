#pragma once

#include <span>
#include <string_view>
#include "span_helpers.h"

std::string_view hexlify(std::span<std::uint8_t const> in, std::span<char> out) {
    out = trim_span(out, in.size() * 2);

    if (in.size() * 2 > out.size()) {
        return{};
    }
    auto nibble2char = [](std::uint8_t v) -> char {
        if (v < 10) {
            return '0' + v;
        }
        if (v < 16) {
            return 'a' + (v - 10);
        }
        assert(false);
        return '?';
    };

    for (auto i=0; i < in.size(); ++i) {
        out[2*i + 0] = nibble2char((in[i] >> 4) & 0x0f);
        out[2*i + 1] = nibble2char((in[i] >> 0) & 0x0f);
    }
    return {out.data(), out.size()};
}