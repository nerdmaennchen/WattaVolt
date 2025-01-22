#include "output.h"

#include "cranc/module/Module.h"
#include "cranc/msg/Message.h"

#include "cranc/config/ApplicationConfig.h"
#include "persistent_config/PersistentConfig.h"

#include <hardware/clocks.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>

namespace {

constexpr std::uint32_t pwm_frequency_hz  = 20'000;

void update_pwm_vals(bool setter);
void update_vals(bool setter);
void update_everything(bool setter);

cranc::MessageBufferMemory<OutputSetpoint, 4> msg_buf;

cranc::ApplicationConfig<std::uint32_t> cfg_feq{ "out.pwm_freq", "I", update_everything, pwm_frequency_hz};

std::array raw_cfgs = {
    cranc::ApplicationConfig<std::uint16_t>{ "vout0.raw", "H", update_pwm_vals, 0},
    cranc::ApplicationConfig<std::uint16_t>{ "iout0.raw", "H", update_pwm_vals, 0},
    cranc::ApplicationConfig<std::uint16_t>{ "vout1.raw", "H", update_pwm_vals, 0},
    cranc::ApplicationConfig<std::uint16_t>{ "iout1.raw", "H", update_pwm_vals, 0},
};


std::array flt_cfgs = {
    cranc::ApplicationConfig<float>{ "vout0", "f", update_vals, 0},
    cranc::ApplicationConfig<float>{ "iout0", "f", update_vals, 0},
    cranc::ApplicationConfig<float>{ "vout1", "f", update_vals, 0},
    cranc::ApplicationConfig<float>{ "iout1", "f", update_vals, 0},
};

using correction_polynomial = std::array<float, 2>;
std::array correction_polynomials = {
    cranc::ApplicationConfig<correction_polynomial>{ "corr.vout0", "2f", {0, 1}},
    cranc::ApplicationConfig<correction_polynomial>{ "corr.iout0", "2f", {0, 1}},
    cranc::ApplicationConfig<correction_polynomial>{ "corr.vout1", "2f", {0, 1}},
    cranc::ApplicationConfig<correction_polynomial>{ "corr.iout1", "2f", {0, 1}},
};

config::PersistentConfig persistence[] = {
    correction_polynomials[0],
    correction_polynomials[1],
    correction_polynomials[2],
    correction_polynomials[3]
};


float apply_polynomial(float x, correction_polynomial const& poly) {
    float x_ = 1;
    float v = 0;
    for (int i=0; i < poly.size(); ++i) {
        v += poly[i] * x_;
        x_ *= x;
    }
    return v;
}

constexpr std::array<float, 4> conversion_scalars = {
    1./ 26.,
    1./ 8.,
    1./ 26.,
    1./ 8.,
};

std::array<float, 4> conversions = {};

constexpr std::array<std::uint8_t, 4> pins {
    6,7,8,9,
};

constexpr std::uint16_t pwm_mask = (1 << 3) | (1 << 4);


struct : cranc::Module {
    using cranc::Module::Module;

    void init() override
    {
        for (auto pin : pins) {
            gpio_set_dir(pin, true);
            gpio_set_function(pin, GPIO_FUNC_PWM);
        }

        update_everything(true);
    }
} _{1000};

void update_pwm_vals(bool setter) {
    if (setter) {
        pwm_set_both_levels(3, *(raw_cfgs[0]), *(raw_cfgs[1]));
        pwm_set_both_levels(4, *(raw_cfgs[3]), *(raw_cfgs[2]));
    }
}

void update_vals(bool setter) {
    if (setter) {
        for (auto i=0; i < 4; ++i) {
            auto corrected = apply_polynomial(*(flt_cfgs[i]), *(correction_polynomials[i]));
            *(raw_cfgs[i]) = corrected * conversions[i];
        }
        update_pwm_vals(true);
        auto msg = msg_buf.getFreeMessage(
            *(flt_cfgs[0]), *(flt_cfgs[1]),
            *(flt_cfgs[2]), *(flt_cfgs[3])
        );
        if (msg) {
            msg->post();
        }
    }
}

void update_everything(bool setter) {
    if (setter) {
        pwm_set_mask_enabled(0);

        pwm_set_counter(3, 0);
        pwm_set_counter(4, 0);

        float system_clock_frequency = clock_get_hz(clk_sys);
        auto max = system_clock_frequency / *cfg_feq;

        pwm_set_wrap(3, max);
        pwm_set_wrap(4, max);

        for (auto i=0; i < 4; ++i) {
            conversions[i] = max * conversion_scalars[i];
        }

        update_vals(true);
        
        pwm_set_mask_enabled(pwm_mask);
    }
}

}
