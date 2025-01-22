#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"

#include "misc/usb_cdc.h"
#include "util/span_helpers.h"


#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include <hardware/pio.h>
#include <hardware/dma.h>

#include "ws2812.pio.h"

#include <array>

namespace {

using namespace usb::literals;
using namespace std::literals::chrono_literals;

PIO pio = pio1;

constexpr auto max_num_leds = 21;

struct Controller {
    usb::USB_String name{"led_control"};

    std::array<char, max_num_leds * 4 * 2 + 2> rx_buffer{};
    std::array<char, max_num_leds * 4> led_buffer{};
    std::size_t rx_buffer_idx{};

    std::uint32_t sm;
    std::uint32_t pio_offset;

    struct DMA {
        int channel;
        dma_channel_config config;
    };
    DMA dma;

    void on_rx_data(std::span<std::uint8_t const> rx) {
        for (int i = 0; i < rx.size(); ++i) {
            rx_buffer[rx_buffer_idx] = rx[i];
            ++rx_buffer_idx;
            if (rx_buffer_idx >= rx_buffer.size()) {
                rx_buffer_idx = 0;
            }
            if ((rx_buffer_idx >= 2) and (rx_buffer[rx_buffer_idx-1] == '\n') and (rx_buffer[rx_buffer_idx-2] == '\r')) {
                // flush the buffer
                auto count_bytes = (rx_buffer_idx - 2) / 2;
                for (auto j=0; j < count_bytes; ++j) {
                    char hi = rx_buffer[2*j+0];
                    char lo = rx_buffer[2*j+1];

                    auto from_hex = [](char c) -> std::uint8_t {
                        if (c >= '0' and c <= '9') {
                            return c - '0';
                        }
                        if (c >= 'a' and c <= 'f') {
                            return 0xa + c - 'a';
                        }
                        if (c >= 'A' and c <= 'F') {
                            return 0xa + c - 'A';
                        }
                        return 0;
                    };

                    led_buffer[j] = (from_hex(lo) << 0) + (from_hex(hi) << 4);
                }

                dma_channel_set_read_addr(dma.channel, led_buffer.data(), false);
                dma_channel_set_trans_count(dma.channel, count_bytes, true);

                rx_buffer_idx = 0;
            }
        }
    }

    USB_CDC_Device cdc;

    std::uint8_t idx2ep(std::uint8_t idx) {
        return 5 + idx*2;
    }

    Controller(std::uint8_t idx, std::uint8_t pin) 
    : cdc{name, std::uint8_t(USB_DIR_OUT|idx2ep(idx)), std::uint8_t(USB_DIR_IN|idx2ep(idx)), std::uint8_t(USB_DIR_IN|(idx2ep(idx)+1)), 
        [this](auto data){ on_rx_data(data); }, 
        [this] {}
    }
    {
        pio_offset = pio_add_program(pio, &ws2812_program);
        sm = pio_claim_unused_sm(pio, true);

        dma.channel =  dma_claim_unused_channel(true);
        dma.config  =  dma_channel_get_default_config(dma.channel);

        channel_config_set_dreq(&dma.config, pio_get_dreq(pio, sm, true));
        channel_config_set_transfer_data_size(&dma.config, DMA_SIZE_8);

        ws2812_program_init(pio, sm, pio_offset, pin, 800'000);
        
        dma_channel_configure(
            dma.channel,
            &dma.config,
            &(pio->txf[sm]),
            led_buffer.data(),
            led_buffer.size(),
            true
        );
    }
};

struct : cranc::Module {
    using cranc::Module::Module;

    std::optional<Controller> controller;

    void init() override
    {
        controller.emplace(0, 16);
    }
} _{10};


}
