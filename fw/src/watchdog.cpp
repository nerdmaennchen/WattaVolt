#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/SwitchToMainLoop.h"

#include "hardware/watchdog.h"

namespace {
using namespace std::literals::chrono_literals;

constexpr auto pace = 500ms;

struct : cranc::Module {
    using cranc::Module::Module;

    cranc::coro::FAFTask work() {
        cranc::coro::AwaitableDelay ticker{cranc::getSystemTime(), pace};
        cranc::coro::SwitchToMainLoop sw2main;

        watchdog_enable(std::chrono::duration_cast<std::chrono::milliseconds>(pace).count() * 2, 1);

        while (true) {
            co_await ticker;
            co_await sw2main;

            watchdog_update();
        }
    }

    void init() override
    {
        work();
    }
} _{1};

}
