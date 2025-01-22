#include "ws2812.h"

#include <cstdlib>

namespace color {

RGB from_string(std::string_view sv) {
    auto start_pos = sv.find_first_of("0123456789aAbBcCdDeEfF");
    RGB rgb{};
    if (start_pos == std::string_view::npos) {
        return rgb;
    }
    auto parsed = std::strtoul(sv.data()+start_pos, nullptr, 16);
    rgb.r = (parsed >> 16) & 0xff;
    rgb.g = (parsed >>  8) & 0xff;
    rgb.b = (parsed >>  0) & 0xff;
    return rgb;
}

}