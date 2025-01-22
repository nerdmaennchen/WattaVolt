#include "cranc/platform/hwTimer.h"
#include "cranc/timer/swTimer.h"
#include "cranc/timer/ISRTime.h"

#include "cranc/module/Module.h"

#include "pico/stdlib.h"

#include <algorithm>

using namespace std::chrono_literals;

namespace cranc
{
namespace platform
{

using granularity = std::chrono::microseconds;

namespace {

std::int64_t timer_elapsed(alarm_id_t, void *)
{
	cranc::ISRTime isrTimer;
	cranc::SWTimer::get().trigger();
	return 0;
}

alarm_id_t alarm_id{};

}

void HWTimer::stop()
{
	if (alarm_id > 0) {
		alarm_pool_cancel_alarm(alarm_pool_get_default(), alarm_id);
		alarm_id = 0;
	}
}

void HWTimer::setup(TimePoint timeout)
{
	if (alarm_id > 0) {
		alarm_pool_cancel_alarm(alarm_pool_get_default(), alarm_id);
		alarm_id = 0;
	}
	auto abs_timeout_us = std::chrono::duration_cast<granularity>(timeout).count();
	abs_timeout_us = std::max<decltype(abs_timeout_us)>(abs_timeout_us, 0);
	absolute_time_t elapse;
	update_us_since_boot(&elapse, static_cast<std::uint64_t>(abs_timeout_us));
	alarm_id = alarm_pool_add_alarm_at(alarm_pool_get_default(), elapse, timer_elapsed, nullptr, true);
}

}
}
