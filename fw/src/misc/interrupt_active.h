#pragma once

// #include <hardware/structs/scb.h>
#include <pico/platform.h>

inline bool isr_active() { 
    auto cur_isr = __get_current_exception();
    return cur_isr != 0;
}