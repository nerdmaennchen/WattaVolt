#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/coro/SwitchToMainLoop.h"
#include "cranc/config/ApplicationConfig.h"
#include "cranc/util/Finally.h"


#include "led/ws2812.h"
#include "misc/usb_cdc.h"
#include "task_utils.h"
#include "awaitable_mutex.h"

#include <cstring>
#include <inttypes.h>
#include <format>


namespace {

struct : cranc::Module {
    using cranc::Module::Module;

    void init() override
    {
    }
} _{1000};

}
