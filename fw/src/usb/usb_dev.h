#pragma once

#include "usb.h"

namespace default_usb_dev {

using namespace usb::literals;

inline auto config_name = "the config"_usb_str;
inline usb::Configuration config{0x80, 250, &config_name};

}
