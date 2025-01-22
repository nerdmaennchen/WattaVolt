#pragma once

#include "cranc/util/function.h"

#include <cstdint>

namespace gpio_irq_multiplexing {

using CB = cranc::function<void(std::uint8_t)>;
void register_irq_cb(std::uint8_t gpio, CB cb);
void unregister_irq_cb(std::uint8_t gpio);

}