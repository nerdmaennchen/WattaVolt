#include "cranc/timer/systemTime.h"
#include "cranc/config/ApplicationConfig.h"

#include "hardware/timer.h"


using TimeResolution = std::chrono::microseconds;

namespace cranc {

TimePoint getSystemTime()
{
    return TimeResolution { time_us_64() };
}

void sleep(Duration duration)
{
    const auto startTime = getSystemTime();
    while ((startTime + duration) > getSystemTime())
        ;
}

}

namespace {

cranc::ApplicationConfig<cranc::TimePoint> times { "system.time", "Q", [](bool setter)
    {
        if (not setter) {
            *times = cranc::getSystemTime();
        }
    }};

}
