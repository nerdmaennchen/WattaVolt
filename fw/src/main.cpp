
#include "cranc/msg/MessagePump.h"
#include "cranc/module/Module.h"
#include "cranc/timer/ISRTime.h"

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <hardware/timer.h>

int main() {
	set_sys_clock_khz(120000, true);
	stdio_init_all();
	timer_hw->dbgpause = 0x0;
	cranc::InitializeModules();

	auto& msgPump = cranc::MessagePump::get();
	while (true) {
		cranc::MessageBase* msg;
		{
			// cranc::LockGuard lock;
			msg = msgPump.frontMessage();
			if (not msg) {
				// __wfi();
				continue;
			}
		}
		cranc::WorkingTime time;
		msg->invoke();
		msg->remove();
	}
	return 0;
}


extern "C" {

int getentropy(void *buf, size_t buflen) {
	// implement this to suppress a linker warning
	return 0;
}

}