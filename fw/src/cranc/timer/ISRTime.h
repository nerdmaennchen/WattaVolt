#pragma once

#include "cranc/platform/system.h"
#include "cranc/timer/systemTime.h"

namespace cranc
{

class WorkingTime final {
public:
    std::chrono::nanoseconds startTime;
    WorkingTime()
    {
        startTime = cranc::getSystemTime();
        if (not current) {
            current = this;
        }
    }
    ~WorkingTime()
    {
        if (current == this) {
            current = nullptr;
            auto dt = cranc::getSystemTime() - startTime;
            timeSpentBusy += dt;
        }
    }
    static inline std::chrono::nanoseconds timeSpentBusy;
    static inline WorkingTime* current;
};

class ISRTime final {
public:
    std::chrono::nanoseconds startTime;

    ISRTime()
    {
        cranc::LockGuard lock;
        startTime = cranc::getSystemTime();
        if (not current) {
            current = this;
        }
    }
    ~ISRTime()
    {
        cranc::LockGuard lock;
        if (current == this) {
            current = nullptr;
            auto dt = cranc::getSystemTime() - startTime;
            timeSpentInISRs += dt;
            if (WorkingTime::current) {
                WorkingTime::current->startTime += dt;
            }
        }
    }

    static inline std::chrono::nanoseconds timeSpentInISRs;
    static inline ISRTime* current;
};

}