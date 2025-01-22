#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"


namespace {
using namespace std::literals::chrono_literals;

struct : cranc::Module {
    using cranc::Module::Module;

    cranc::coro::Task<void> task;
    cranc::coro::Task<void> work() {
        cranc::coro::AwaitableDelay ticker{cranc::getSystemTime() + 100ms, 100ms};
        while (true) {
            co_await ticker;
        }
    }

    void init() override
    {
        task = work();
    }
} _{1000};

}
