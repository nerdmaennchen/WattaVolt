#pragma once

#include "cranc/timer/systemTime.h"

#include <variant>
namespace buttons
{

enum class button {
    btn0,
    btn1,
};

enum class state {
    pressed,
    released,
};

struct Event {
    button btn;
    state st;
};

struct PressedFor {
    button btn;
    cranc::Duration duration;
};

}
