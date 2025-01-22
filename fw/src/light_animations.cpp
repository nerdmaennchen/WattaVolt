#include "light_animations.h"

#include "cranc/coro/Generator.h"

#include "util/random.h"

#include <chrono>
#include <numbers>
#include <cmath>
#include <array>
#include <random>

namespace light_animations {

using namespace std::literals::chrono_literals;

namespace {

constexpr auto tick_duration = 33ms;

using float_ts = std::chrono::duration<float>;

float floatify_ts(cranc::TimePoint tp = cranc::getSystemTime()) {
    return std::chrono::duration_cast<float_ts>(tp).count();
}

constexpr auto pi = std::numbers::pi_v<float>;
constexpr auto tau = 2.f * pi;

template<std::size_t N>
consteval std::array<float, N> linspace(float min, float max) {
    auto ret = std::array<float, N>{};

    for (auto i = 0; i < N; ++i) {
        ret[i] = std::lerp(min, max, float(i) / float{N});
    }
    return ret;
}

std::unique_ptr<float[]> linspace(float min, float max, std::size_t N) {
    auto ret = std::make_unique<float[]>(N);

    for (auto i = 0; i < N; ++i) {
        ret[i] = std::lerp(min, max, float(i) / N);
    }
    return ret;
}

constexpr auto my_sq(auto v) {
    return v * v;
}

void scale_colors(color::RGB color, std::span<float const> scales, WS2812::LED_Buffer& buf) {
    assert(scales.size() == buf.size());
    for (auto i = 0; i < buf.size(); ++i) {
        buf[i].w = color.w * scales[i];
        buf[i].b = color.b * scales[i];
        buf[i].r = color.r * scales[i];
        buf[i].g = color.g * scales[i];
    }
}

void normal_arr(std::span<float> arr, float mean=0, float std_dev=1) {
    RP2040_rand_dev rd;
    std::mt19937 gen{rd()};
    std::normal_distribution<float> d{mean, std_dev};
    for (auto& a : arr) {
        a = d(gen);
    }
}

cranc::coro::Generator<float> pacer(float animation_duration) {
    float start = floatify_ts(cranc::getSystemTime());
    while (true) {
        auto now = floatify_ts();
        co_yield (now - start) / animation_duration;
    }
}

struct AutoOff {
    WS2812::LED_Buffer leds;
    AutoOff(WS2812::LED_Buffer& dev) : leds{dev} {}
    ~AutoOff() {
        for (auto& b : leds) {
            b.grb = 0;
        }
    }
};

}

Animation scan(color::RGB color, float duration, WS2812::LED_Buffer buffer) {
    AutoOff ao{buffer};
    auto pace = pacer(duration);
    auto xx = linspace(-1, 1, buffer.size());
    auto yy_ = std::make_unique<float[]>(buffer.size());
    auto yy = std::span<float>(yy_.get(), buffer.size());
    constexpr auto var = .15f;

    while (pace.advance()) {
        auto tick = pace.get();
        auto x = std::sin(tick * tau);
        for (auto i = 0; i < buffer.size(); ++i) {
            yy[i] = std::exp(-.5f * my_sq((xx[i] - x) / var));
        }
        scale_colors(color, yy, buffer);
        co_yield {};
    }
}

Animation glow(color::RGB color, float duration, WS2812::LED_Buffer buffer) {
    AutoOff ao{buffer};
    auto pace = pacer(duration);
    auto yy_ = std::make_unique<float[]>(buffer.size());
    auto yy = std::span<float>(yy_.get(), buffer.size());

    while (pace.advance()) {
        auto tick = pace.get();
        auto x = .5f + -std::cos(tick * tau) * .5f;
        for (auto i = 0; i < buffer.size(); ++i) {
            buffer[i].w = color.w * x;
            buffer[i].b = color.b * x;
            buffer[i].r = color.r * x;
            buffer[i].g = color.g * x;
        }
        co_yield {};
    }
}

Animation twinkle(color::RGB color, float duration, WS2812::LED_Buffer buffer) {
    AutoOff ao{buffer};
    auto pace = pacer(duration);
    auto yy_ = std::make_unique<float[]>(buffer.size());
    auto yy = std::span<float>(yy_.get(), buffer.size());

    auto offsets_ = std::make_unique<float[]>(buffer.size());
    auto offsets  = std::span<float>(offsets_.get(), buffer.size());
    normal_arr(offsets, 0, 1);

    auto speeds_ = std::make_unique<float[]>(buffer.size());
    auto speeds  = std::span<float>(speeds_.get(), buffer.size());
    normal_arr(speeds, duration, .5);

    while (pace.advance()) {
        auto tick = pace.get();
        for (auto i = 0; i < buffer.size(); ++i) {
            auto x = .5f + -std::cos(tick * tau * speeds[i] + offsets[i]) * .5f;
            buffer[i].w = color.w * x;
            buffer[i].b = color.b * x;
            buffer[i].r = color.r * x;
            buffer[i].g = color.g * x;
        }
        co_yield {};
    }
}

Animation on(color::RGB color, WS2812::LED_Buffer buffer) {
    while (true) {
        std::fill(buffer.begin(), buffer.end(), color);
        co_yield {};
    }
}


struct LED_Segment {
    std::uint8_t begin_idx, end_idx;
    color::RGB color;
};

const std::array<LED_Segment, 5> segments {
    LED_Segment {
        .begin_idx = 8,
        .end_idx = 13,
        .color = color::ugly_purple
    },
    LED_Segment {
        .begin_idx = 3,
        .end_idx = 8,
        .color = color::red
    },
    LED_Segment {
        .begin_idx = 13,
        .end_idx = 18,
        .color = color::green
    },
    LED_Segment {
        .begin_idx = 0,
        .end_idx = 3,
        .color = color::blue
    },
    LED_Segment {
        .begin_idx = 18,
        .end_idx = 21,
        .color = color::yellow
    },
};

// cranc::coro::Task<void> indicate_tags(std::size_t cnt_active) {
//     cranc::coro::AwaitableClaim<WS2812> claim;
//     WS2812::claim(claim);
//     decltype(claim)::Access dev = co_await claim;

//     WS2812::LED_Buffer buf{};

//     cnt_active = std::min(cnt_active, segments.size());

//     for (auto i=0; i < cnt_active; ++i) {
//         auto const& seg = segments[i];
//         for (auto c = seg.begin_idx; c < seg.end_idx; ++c) {
//             buf[c] = seg.color;
//         }
//     }
//     dev->flush(buf);

//     co_await cranc::coro::AwaitableDelay(1ms);
// }

}