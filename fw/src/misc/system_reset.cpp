
#include "cranc/config/ApplicationConfig.h"

#include "pico/bootrom.h"
#include "hardware/watchdog.h"

namespace
{

cranc::ApplicationConfig<void> rst{"system.reset", []{
    watchdog_reboot(0, 0, 0);
}};

cranc::ApplicationConfig<void> enter_bl{"system.enter_bootloader", []{ 
    reset_usb_boot(0, 0);
}};

}
