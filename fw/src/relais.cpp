#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/config/ApplicationConfig.h"

#include "cranc/msg/Message.h"

#include <hardware/gpio.h>

namespace {
using namespace std::literals::chrono_literals;

constexpr auto enable_debounce = 100ms;

struct EnableCMD {
    std::uint8_t channel;
    bool enable;
};

cranc::MessageBufferMemory<EnableCMD, 4> msg_buffer;

cranc::ApplicationConfig<std::uint8_t> output_0_enable { "output0.enable", "B", [](bool setter)
{
    if (setter) {
        auto msg = msg_buffer.getFreeMessage(EnableCMD{0, *output_0_enable != 0});
        if (not msg) {
            return;
        }
        msg->post();
    }
} };
cranc::ApplicationConfig<std::uint8_t> output_1_enable { "output1.enable", "B", [](bool setter)
{
    if (setter) {
        auto msg = msg_buffer.getFreeMessage(EnableCMD{1, *output_1_enable != 0});
        if (not msg) {
            return;
        }
        msg->post();
    }
} };

struct : cranc::Module {
    using cranc::Module::Module;

    cranc::coro::FAFTask work(std::uint8_t channel, std::uint8_t gnd_en, std::uint8_t out_en) {

        gpio_init(gnd_en);
        gpio_init(out_en);

        gpio_set_dir(gnd_en, GPIO_OUT);
        gpio_set_dir(out_en, GPIO_OUT);

        cranc::coro::Awaitable<EnableCMD> event;
        cranc::Listener<EnableCMD> listener{[&](auto const& msg) { 
            if (msg.channel == channel){
                event(msg);
            }
        }};

        while (true) {
            auto msg = co_await event;
            if (msg.enable) {
                gpio_put(gnd_en, true);
                co_await cranc::coro::AwaitableDelay{enable_debounce};
                gpio_put(out_en, true);
            } else {
                gpio_put(out_en, false);
                co_await cranc::coro::AwaitableDelay{enable_debounce};
                gpio_put(gnd_en, false);
            }
        }

        co_return;
    }

    void init() override
    {
        output_0_enable.set(0);
        output_1_enable.set(0);
        work(0, 4, 5);
        work(1, 2, 3);
    }
} _{1000};

}
