#include "cranc/timer/ISRTime.h"

#include "cranc/config/ApplicationConfig.h"
#include <array>

namespace {

cranc::ApplicationConfig<std::array<uint64_t, 3>> isr_times { "system.load", "3Q", [](bool setter)
{
    if (not setter) {
        auto curTime = cranc::getSystemTime();
        (*isr_times)[0] = curTime.count();
        (*isr_times)[1] = cranc::ISRTime::timeSpentInISRs.count();
        (*isr_times)[2] = cranc::WorkingTime::timeSpentBusy.count();
    }
} };

}
