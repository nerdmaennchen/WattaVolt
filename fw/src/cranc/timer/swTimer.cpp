
#include "cranc/platform/system.h"

#include "cranc/timer/swTimer.h"
#include "cranc/timer/systemTime.h"
#include "cranc/platform/hwTimer.h"
#include "cranc/util/Finally.h"

#include <algorithm>
#include <cassert>

using namespace std::chrono_literals;
namespace cranc
{

using HWTimer = cranc::platform::HWTimer;
void SWTimer::registerTimer(Timer& cb)
{
	assert(cb.empty());
	LockGuard lock;
	mTimers.insertNext(&cb);
	setupTimer();
}

void SWTimer::unregisterTimer(Timer& cb)
{
	if (cb.empty()) {
		return;
	}
	LockGuard lock;
	cb.remove();
	setupTimer();
}

void SWTimer::setupTimer()
{
	LockGuard lock;
	if (trigger_handler_active) {
		return;
	}
	HWTimer& tim = HWTimer::get();
	tim.stop();

	auto nextTimeout = TimePoint::max();
	for (auto& timer : mTimers) {
		nextTimeout = std::min(nextTimeout, timer->mTimeout);
	}
	if (not mTimers.empty()) {
		tim.setup(nextTimeout);
	}
}

void SWTimer::trigger()
{
	{
		LockGuard lock;
		trigger_handler_active = true;
	}

	const auto now = getSystemTime();
	for (auto& t : mTimers) {
		if (now > t->mTimeout) {
			int elapsed = 1;
			if (t->mDelay <= 0s) {
				t.remove();
			} else {
				elapsed += (now - t->mTimeout) / t->mDelay;
				t->mTimeout += elapsed * t->mDelay;
			}
			t->mCB(elapsed);
		}
	}
	{
		LockGuard lock;
		trigger_handler_active = false;
		setupTimer();
	}
}

}
