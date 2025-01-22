#include "cranc/module/Module.h"

#include "cranc/msg/Message.h"
#include "cranc/msg/Listener.h"

#include "cranc/timer/swTimer.h"

#include <array>
#include <algorithm>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace
{

struct BlinkInstruction {
	std::chrono::nanoseconds delay;
	bool ledState;
};

using namespace std::chrono_literals;
std::array<BlinkInstruction, 4> heartBeatSequence {{
	{600ms, false},
	{70ms, true},
	{100ms, false},
	{50ms, true}
}};


struct HeartBeat : cranc::Module
{
    using cranc::Module::Module;

	struct HeartbeatMsg {
		std::size_t i;
	};

	cranc::MessageBufferMemory<HeartbeatMsg, 2> msgBuf;

	cranc::Listener<HeartbeatMsg> msg_wrapper {[this](auto const& msg){ HeartBeat::on_msg(msg); }};
	cranc::Timer                 timer {[this](int elapses){ onTimer(elapses); }};

	std::size_t i{};
	std::chrono::nanoseconds nextTick{};

	void onTimer(int) {
		auto msg = msgBuf.getFreeMessage();
		if (msg) {
			++i;
			if (i == heartBeatSequence.size()) {
				i = 0;
			}
			(*msg)->i = i;
			msg->post();
		}
	}

	void on_msg(HeartbeatMsg const& msg) {
		if (heartBeatSequence[msg.i].ledState) {
    		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
		} else {
    		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
        }
		nextTick += heartBeatSequence[msg.i].delay;
		timer.start(nextTick);
	}

	void init() override {
		auto msg = msgBuf.getFreeMessage();
		if (msg) {
			(*msg)->i = 0;
			msg->post();
		}
	}
} heartBeat{10};

}


