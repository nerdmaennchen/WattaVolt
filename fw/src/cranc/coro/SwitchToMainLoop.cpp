#include "SwitchToMainLoop.h"
#include "misc/interrupt_active.h"

#include <hardware/structs/scb.h>

namespace cranc::coro
{

bool SwitchToMainLoop::await_ready() { 
    return not isr_active();
    // return false; 
}

namespace {
cranc::Listener<SwitchToMainLoop::SwitchMsg> trampoline {[](SwitchToMainLoop::SwitchMsg const& msg){
    assert(msg.handle);
    msg.handle();
}};
}
}