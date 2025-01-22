#pragma once


#include "cranc/coro/Task.h"
#include "cranc/coro/Generator.h"
#include "led/ws2812.h"

namespace light_animations {

using Animation = cranc::coro::Generator<void>;

Animation scan(color::RGB color, float duration, WS2812::LED_Buffer buffer);
Animation glow(color::RGB color, float duration, WS2812::LED_Buffer buffer);
Animation twinkle(color::RGB color, float duration, WS2812::LED_Buffer buffer);

Animation on(color::RGB color, WS2812::LED_Buffer buffer);

// Gen indicate_tags(WS2812& leds, std::size_t cnt_active);

}