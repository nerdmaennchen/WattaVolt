#pragma once

#include <inttypes.h>
#include <cranc/util/Singleton.h>

#include <chrono>

namespace cranc
{

using TimePoint = std::chrono::nanoseconds;
using Duration = TimePoint;

TimePoint getSystemTime();

void sleep(Duration duration);

}
