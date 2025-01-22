#pragma once

#include "cranc/timer/systemTime.h"
#include "cranc/util/Singleton.h"

namespace cranc
{
namespace platform
{

struct HWTimer : cranc::util::Singleton<HWTimer> {
	void setup(TimePoint timeout);
	void stop();
};

}
}
