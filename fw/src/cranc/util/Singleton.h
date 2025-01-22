#pragma once

#include "cranc/platform/system.h"

namespace cranc
{
namespace util
{

template<class T>
struct Singleton {
	static T& get() {
        // need a lock guard to prevent race conditions when compiling with no-threadsafe-statics
		LockGuard lock;
		static T instance;
		return instance;
	}
};

}
}

