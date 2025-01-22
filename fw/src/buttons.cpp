#include "buttons.h"

#include "cranc/module/Module.h"
#include "cranc/msg/Message.h"

#include "cranc/config/ApplicationConfig.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/util/Finally.h"

#include "misc/gpio_irq_multiplexing.h"

#include "hardware/gpio.h"

namespace {

using namespace std::literals::chrono_literals;

constexpr auto debounce_time = 20ms;

cranc::ApplicationConfig<std::uint8_t> btn_cfg { "buttons", "B" };


struct : cranc::Module {
    using cranc::Module::Module;

    cranc::MessageBufferMemory<buttons::Event, 8> btn_event_msg_buffer;
    cranc::MessageBufferMemory<buttons::PressedFor, 8> pressed_for_msg_buffer;

    void post_state(buttons::Event event) {
        auto msg = btn_event_msg_buffer.getFreeMessage(event);

        {
            cranc::LockGuard lock;
            auto val = *btn_cfg;
            if (event.st == buttons::state::pressed) {
                val |= 1 << static_cast<std::uint8_t>(event.btn);
            } else {
                val &= ~(1 << static_cast<std::uint8_t>(event.btn));
            }
            *btn_cfg = val;
        }

        if (not msg) {
            return;
        }
        msg->post();
    }

    cranc::coro::FAFTask listen(buttons::button btn, std::uint8_t pin) {
        gpio_init(pin);
        gpio_set_pulls(pin, true, false);

        cranc::coro::Awaitable<void, cranc::LockGuard> event;

        gpio_irq_multiplexing::register_irq_cb(pin, [&](std::uint8_t button) {
            gpio_set_irq_enabled(pin, 0x0f, false);
            event();
        });

        while (true) {
            gpio_set_irq_enabled(pin, GPIO_IRQ_LEVEL_LOW, true);
            // wait for a low
            co_await event;
            co_await cranc::coro::AwaitableDelay{debounce_time};
            if (gpio_get(pin)) {
                continue;
            }

            post_state({btn, buttons::state::pressed});

            // wait for a high
            while (true) {
                gpio_set_irq_enabled(pin, GPIO_IRQ_LEVEL_HIGH, true);
                co_await event;
                co_await cranc::coro::AwaitableDelay{debounce_time};
                if (gpio_get(pin)) {
                    break;
                }
            }
            post_state({btn, buttons::state::released});
        }
    }

    void post_press_duration(buttons::PressedFor event) {
        auto msg = pressed_for_msg_buffer.getFreeMessage(event);
        if (not msg) {
            return;
        }
        msg->post();
    }

    void init() override
    {
        listen(buttons::button::btn0, 20);
        listen(buttons::button::btn1, 21);
    }
} _{1000};

}