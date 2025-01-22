#pragma once

#include "cranc/util/Claimable.h"
#include "cranc/util/function.h"

#include "ws2812.pio.h"

#include <hardware/pio.h>
#include <hardware/dma.h>

#include <cstdint>
#include <array>
#include <span>
#include <string_view>

namespace color {
    union __attribute__((packed)) RGB {
        struct {
            std::uint8_t w, b, r, g;
        };
        struct {
            std::uint32_t padding:8;
            std::uint32_t grb:24;
        };
    };
    static_assert(sizeof(RGB) == 4);

    static inline constexpr auto ugly_purple = RGB{
        .padding = 0, .grb = 0x005256,
    };
    static inline constexpr auto red = RGB{
        .padding = 0, .grb = 0x005200,
    };
    static inline constexpr auto green = RGB{
        .padding = 0, .grb = 0x520000,
    };
    static inline constexpr auto blue = RGB{
        .padding = 0, .grb = 0x000052,
    };
    static inline constexpr auto yellow = RGB{
        .padding = 0, .grb = 0x290029,
    };
    static inline constexpr auto black = RGB{
        .padding = 0, .grb = 0x000000,
    };

    RGB from_string(std::string_view sv);
}

struct WS2812 {
    using LED_Buffer = std::span<color::RGB>;
    using LED_Buffer_c = std::span<color::RGB const>;

    void flush(LED_Buffer_c data) {
        dma_channel_set_trans_count(dma.channel, data.size(), false);
        dma_channel_set_read_addr(dma.channel, data.data(), true);
    }

    WS2812(std::uint8_t pin) {
        pio_offset = pio_add_program(pio, &ws2812_program);
        sm = pio_claim_unused_sm(pio, true);

        dma.channel =  dma_claim_unused_channel(true);
        dma.config  =  dma_channel_get_default_config(dma.channel);

        channel_config_set_dreq(&dma.config, pio_get_dreq(pio, sm, true));
        channel_config_set_transfer_data_size(&dma.config, DMA_SIZE_32);

        dma_channel_configure(
            dma.channel,
            &dma.config,
            &(pio->txf[sm]),
            nullptr,
            0,
            false
        );
        ws2812_program_init(pio, sm, pio_offset, pin, 800'000, 3);
    }

    ~WS2812() {
        pio_remove_program_and_unclaim_sm(&ws2812_program, pio, sm, pio_offset);
        dma_channel_unclaim(dma.channel);
    }

protected:
    int sm;
    int pio_offset;

    struct DMA {
        int channel;
        dma_channel_config config;
    };
    DMA dma;
    static inline PIO pio = pio1;
};
