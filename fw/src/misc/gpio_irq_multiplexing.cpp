#include "gpio_irq_multiplexing.h"

#include "cranc/platform/system.h"

#include "hardware/gpio.h"

#include <array>

namespace gpio_irq_multiplexing {

namespace {

std::array<CB, 32> cbs{};

bool initialized{};

}

void gpio_callback(uint gpio, std::uint32_t events) {
    cbs[gpio](gpio);
}

void register_irq_cb(std::uint8_t gpio, CB cb) {
    cranc::LockGuard lock;
    cbs[gpio] = std::move(cb);
    if (not initialized) {
        gpio_set_irq_callback(gpio_callback);
        irq_set_enabled(IO_IRQ_BANK0, true);
        initialized = true;
    }
}

void unregister_irq_cb(std::uint8_t gpio) {
    cranc::LockGuard lock;
    cbs[gpio] = {};
}

}