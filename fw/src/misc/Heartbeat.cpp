#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/coro/SwitchToMainLoop.h"

#include "led/ws2812.h"
#include "util/span_helpers.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace
{

constexpr std::uint8_t defaultBrightness = 4;
constexpr color::RGB on_color {
	.w = 0,
	.b=0,
	.r=0,
	.g=defaultBrightness,
};

constexpr auto off_color = color::black;

struct BlinkInstruction {
	std::chrono::nanoseconds delay;
	color::RGB color;
};

using namespace std::chrono_literals;
std::array<BlinkInstruction, 4> heartBeatSequence {{
	{600ms, off_color},
	{70ms, on_color},
	{100ms, off_color},
	{50ms, on_color}
}};


struct HeartbeatMsg {
	std::size_t i;
};

struct HeartBeat : cranc::Module
{
    using cranc::Module::Module;

	cranc::coro::Task<void> job;

	cranc::coro::Task<void> runner() {
		cranc::coro::AwaitableDelay timer_ticker{};
		cranc::coro::SwitchToMainLoop sw2main;
		WS2812 dev{29};

		cranc::TimePoint next_timeout{0};
		while (true) {
			for (auto const& s : heartBeatSequence) {
				co_await sw2main;
				{
					dev.flush(to_span_c<color::RGB const>(s.color));
					next_timeout += s.delay;
					timer_ticker.start(next_timeout);
				}
				co_await timer_ticker;
			}
		}
	}

	void init() override {
		job = runner();
	}
} heartBeat{10};

}


