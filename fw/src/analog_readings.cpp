#include "analog_readings.h"

#include "cranc/module/Module.h"
#include "cranc/msg/Message.h"

#include "util/ScaledNumber.h"

#include "cranc/config/ApplicationConfig.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"

#include "i2c/i2c.h"

#include "misc/gpio_irq_multiplexing.h"
#include "hardware/gpio.h"

namespace {

cranc::ApplicationConfig<std::array<std::int16_t, 4>> adc_raw_config {"adc.raw",  "4H"};
cranc::ApplicationConfig<std::array<float, 4>> adc_config {"adc",  "4f"};

cranc::MessageBufferMemory<AnalogReadings, 4> msg_buf;

using namespace std::literals::chrono_literals;

constexpr std::uint8_t i2c_addr = 0b1001'000;

constexpr auto rdy_pin = 10;

constexpr std::array<std::uint8_t, 4> channel_selects {
    0b1'110'000'1, // OUT_SENSE_0 2
    0b1'111'000'1, // CUR_SENSE_0 3
    0b1'101'000'1, // OUT_SENSE_1 1
    0b1'100'000'1, // CUR_SENSE_1 0
};

constexpr std::array<float, 4> reading_to_value_scales {
    6.144 / ((1 << 15) - 1) * (91+13)/13,
    6.144 / ((1 << 15) - 1) / (100 * 5e-3),
    6.144 / ((1 << 15) - 1) * (91+13)/13,
    6.144 / ((1 << 15) - 1) / (100 * 5e-3),
};

struct : cranc::Module {
    using cranc::Module::Module;


    cranc::coro::FAFTask monitor() {
        gpio_init(rdy_pin);

        cranc::coro::Awaitable<bool, cranc::LockGuard> alert;
        gpio_irq_multiplexing::register_irq_cb(rdy_pin, [&](std::uint8_t button) {
            gpio_set_irq_enabled(rdy_pin, 0x0f, false);
            alert(false);
        });

        cranc::coro::AwaitableClaim<I2C> claim{};
        cranc::coro::Awaitable<bool> i2cDone;
        auto i2cDoneF = [&i2cDone](bool b) { i2cDone(b); };

        restart:
        *adc_config = {};
        while (true) {
            co_await cranc::coro::AwaitableDelay{1s};
            {
                I2C::claim(claim);
                auto i2c = co_await claim;

                i2c->set_addr(i2c_addr);

                { // see if a conversion is ongoing
                    auto data = std::array<std::uint8_t, 1>{0x01};
                    i2c->write(data, false);
                    auto rx_data = std::array<std::uint8_t, 2>{};
                    i2c->read(rx_data, true, i2cDoneF);
                    if (not co_await i2cDone) { goto restart; }
                    if ((rx_data[0] & 0x80) == 0) { // device is busy
                        goto restart;
                    }
                }
                {
                    // flush the most recent sample
                    auto data = std::array<std::uint8_t, 1>{0x00};
                    i2c->write(data, false);
                    auto rx_data = std::array<std::uint8_t, 2>{};
                    i2c->read(rx_data, true, i2cDoneF);
                    if (not co_await i2cDone) { goto restart; }
                }

                // set low thresh to 0 and high thresh to 0xffff
                auto data_lo = std::array<std::uint8_t, 3>{0x02, 0x00, 0x00};
                i2c->write(data_lo, true);
                i2c->sync(i2cDoneF);
                if (not co_await i2cDone) { goto restart; }
                co_await cranc::coro::AwaitableDelay{10ms};

                auto data_hi = std::array<std::uint8_t, 3>{0x03, 0xff, 0xff};
                i2c->write(data_hi, true);
                i2c->sync(i2cDoneF);
                if (not co_await i2cDone) { goto restart; }
                co_await cranc::coro::AwaitableDelay{10ms};
            }

            while (true) {

                for (int i=0; i < 4; ++i) {
                    {
                        I2C::claim(claim);
                        auto i2c = co_await claim;
                        i2c->set_addr(i2c_addr);
                        // trigger a conversion
                        auto data = std::array<std::uint8_t, 3>{0x01, channel_selects[i], 0b111'0'1'1'00};
                        i2c->write(data, true);
                        i2c->sync(i2cDoneF);
                        if (not co_await i2cDone) { goto restart; }
                    }

                    alert.clear();
                    gpio_set_irq_enabled(rdy_pin, GPIO_IRQ_LEVEL_HIGH, true);

                    cranc::Timer timeout_timer{[&](int){
                        alert(true);
                    }, cranc::getSystemTime() + 50ms};
                    
                    if (co_await alert) {
                        goto restart;
                    }

                    {
                        I2C::claim(claim);
                        auto i2c = co_await claim;
                        i2c->set_addr(i2c_addr);
                        auto data = std::array<std::uint8_t, 1>{0x00};
                        i2c->write(data, false);
                        auto rx_data = std::array<std::uint8_t, 2>{};
                        i2c->read(rx_data, true, i2cDoneF);
                        if (not co_await i2cDone) { goto restart; }
                        (*adc_raw_config)[i] = (rx_data[0] << 8) | (rx_data[1] << 0);
                        (*adc_config)[i] = (*adc_raw_config)[i] * reading_to_value_scales[i];
                    }
                }
                auto msg = msg_buf.getFreeMessage(
                    (*adc_config)[0], (*adc_config)[1],
                    (*adc_config)[2], (*adc_config)[3]
                );
                if (msg) {
                    msg->post();
                }
            }

        }
    }

    void init() override
    {
        monitor();
    }
} _{1000};

}