#pragma once

#include "cranc/platform/hwTimer.h"
#include "cranc/util/function.h"
#include "cranc/util/Singleton.h"
#include "cranc/util/LinkedList.h"

#include <cstdint>
#include <chrono>

namespace cranc
{

using TimerInterval = std::chrono::nanoseconds;

struct Timer;
struct SWTimer : cranc::util::Singleton<SWTimer>
{
	void registerTimer(Timer& callback);
	void unregisterTimer(Timer& callback);

	void trigger();
private:
	void setupTimer();
	cranc::util::LinkedList<Timer> mTimers;
	bool trigger_handler_active;
};

struct Timer : cranc::util::LinkedList<Timer> {
	Timer(cranc::function<void(int)> cb) 
		: mCB{std::move(cb)}
	{}
	Timer(cranc::function<void(int)> cb, TimerInterval timeout, TimerInterval delay=TimerInterval::zero()) 
		: mCB{std::move(cb)}
	{
		start(timeout, delay);
	}

	~Timer() {
		stop();
	}

	void start(TimerInterval timeout, TimerInterval delay=TimerInterval::zero()) {
		mTimeout = timeout;
		mDelay = delay;
		SWTimer::get().registerTimer(*this);
	}

	void stop() {
		SWTimer::get().unregisterTimer(*this);
	}

	Timer(Timer&&) = delete;
	Timer(Timer const&) = delete;

	Timer& operator=(Timer&&) = delete;
	Timer& operator=(Timer const&) = delete;

private:
	friend class cranc::SWTimer;
    
	// when this timer elapses next
	TimerInterval mTimeout  {};

	// delay between successive timouts
	TimerInterval mDelay {};

	cranc::function<void(int)> mCB;
};

}
