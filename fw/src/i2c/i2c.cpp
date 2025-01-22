#include "i2c.h"

#include "cranc/util/FiFo.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/coro/Task.h"
#include "cranc/util/Finally.h"

#include "util/Finally.h"

#include <hardware/i2c.h>
#include <hardware/resets.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>

#include <variant>

namespace 
{

using namespace std::literals::chrono_literals;

i2c_hw_t* i2c_instance = i2c0_hw;
constexpr auto pin_sda = 0;
constexpr auto pin_scl = 1;

void i2c_reset(i2c_hw_t *i2c) {
    reset_block(i2c == i2c0_hw ? RESETS_RESET_I2C0_BITS : RESETS_RESET_I2C1_BITS);
}

void i2c_unreset(i2c_hw_t *i2c) {
    unreset_block_wait(i2c == i2c0_hw ? RESETS_RESET_I2C0_BITS : RESETS_RESET_I2C1_BITS);
}


struct SetAddr {
    std::uint8_t addr;
};

struct Write {
    std::span<const std::uint8_t> data;
    bool start_with_restart;
    bool stop_at_end;
};

struct Read {
    std::span<std::uint8_t> data;
    bool start_with_restart;
    bool stop_at_end;
};

struct Sync {};
struct RecoverBus {};

struct Action {
    std::variant<SetAddr, Write, Read, Sync, RecoverBus> action;
    I2C::CB cb;
};

cranc::FIFO<Action, 8, cranc::LockGuard> queue;
cranc::coro::Awaitable<void, cranc::LockGuard> queue_tick;
cranc::coro::Awaitable<void, cranc::LockGuard> i2c_tick;

cranc::coro::Task<void> worker_task;


constexpr auto error_irq_mask = I2C_IC_INTR_MASK_M_TX_ABRT_BITS;

std::uint32_t error(std::uint32_t mask=0) {
    return i2c_instance->raw_intr_stat & (error_irq_mask | mask);
}

std::uint32_t clear_error() {
    auto stat = i2c_instance->clr_tx_abrt;
    if (stat) {
        // for reasons beyond my understanding, this sleep seems necessary
        // otherwise some addr-nacks won't be detcted -.-
        cranc::sleep(10us);
    }
    return stat;
}

// std::uint32_t set_baudrate(std::uint32_t  baud) {
//     uint freq_in = clock_get_hz(clk_sys);

//     // TODO there are some subtleties to I2C timing which we are completely ignoring here
//     uint period = (freq_in + baud / 2) / baud;
//     uint lcnt = period * 3 / 5; // oof this one hurts
//     uint hcnt = period - lcnt;

//     // Check for out-of-range divisors:
//     invalid_params_if(I2C, hcnt > I2C_IC_FS_SCL_HCNT_IC_FS_SCL_HCNT_BITS);
//     invalid_params_if(I2C, lcnt > I2C_IC_FS_SCL_LCNT_IC_FS_SCL_LCNT_BITS);
//     invalid_params_if(I2C, hcnt < 8);
//     invalid_params_if(I2C, lcnt < 8);

//     // Per I2C-bus specification a device in standard or fast mode must
//     // internally provide a hold time of at least 300ns for the SDA signal to
//     // bridge the undefined region of the falling edge of SCL. A smaller hold
//     // time of 120ns is used for fast mode plus.
//     uint sda_tx_hold_count;
//     if (baud < 1000000) {
//         // sda_tx_hold_count = freq_in [cycles/s] * 300ns * (1s / 1e9ns)
//         // Reduce 300/1e9 to 3/1e7 to avoid numbers that don't fit in uint.
//         // Add 1 to avoid division truncation.
//         sda_tx_hold_count = ((freq_in * 3) / 10000000) + 1;
//     } else {
//         // sda_tx_hold_count = freq_in [cycles/s] * 120ns * (1s / 1e9ns)
//         // Reduce 120/1e9 to 3/25e6 to avoid numbers that don't fit in uint.
//         // Add 1 to avoid division truncation.
//         sda_tx_hold_count = ((freq_in * 3) / 25000000) + 1;
//     }
//     assert(sda_tx_hold_count <= lcnt - 2);

//     i2c_instance->enable = 0;
//     // Always use "fast" mode (<= 400 kHz, works fine for standard mode too)
//     hw_write_masked(&i2c_instance->con,
//                    I2C_IC_CON_SPEED_VALUE_FAST << I2C_IC_CON_SPEED_LSB,
//                    I2C_IC_CON_SPEED_BITS
//     );
//     i2c_instance->fs_scl_hcnt = hcnt;
//     i2c_instance->fs_scl_lcnt = lcnt;
//     i2c_instance->fs_spklen = lcnt < 16 ? 1 : lcnt / 16;
//     hw_write_masked(&i2c_instance->sda_hold,
//                     sda_tx_hold_count << I2C_IC_SDA_HOLD_IC_SDA_TX_HOLD_LSB,
//                     I2C_IC_SDA_HOLD_IC_SDA_TX_HOLD_BITS);

//     i2c_instance->enable = 1;
//     return freq_in / period;
// }

void recover() {
    i2c_reset(i2c_instance);
    i2c_unreset(i2c_instance);

    i2c_instance->enable = 0;

    // Configure as a fast-mode master with RepStart support, 7-bit addresses
    i2c_instance->con =
            I2C_IC_CON_SPEED_VALUE_FAST << I2C_IC_CON_SPEED_LSB |
            I2C_IC_CON_MASTER_MODE_BITS |
            I2C_IC_CON_IC_SLAVE_DISABLE_BITS |
            I2C_IC_CON_IC_RESTART_EN_BITS |
            I2C_IC_CON_TX_EMPTY_CTRL_BITS;

    // Set FIFO watermarks to 1 to make things simpler. This is encoded by a register value of 0.
    i2c_instance->tx_tl = 0;
    i2c_instance->rx_tl = 0;

    // Always enable the DREQ signalling -- harmless if DMA isn't listening
    i2c_instance->dma_cr = I2C_IC_DMA_CR_TDMAE_BITS | I2C_IC_DMA_CR_RDMAE_BITS;


    i2c_inst  instance = {
        .hw = i2c_instance,
        .restart_on_next = false
    };
    i2c_set_baudrate(&instance, 100'000);

    i2c_instance->intr_mask = error_irq_mask;

    gpio_pull_up(pin_sda);
    gpio_pull_up(pin_scl);
    gpio_set_function(pin_sda, GPIO_FUNC_I2C);
    gpio_set_function(pin_scl, GPIO_FUNC_I2C);
}


cranc::coro::Task<void> work() {
    recover:
    recover();
    while (true) {
        next:
        while (queue.count() == 0) {
            co_await queue_tick;
        }
        auto const& action = queue[0];

        const auto fin = cranc::Finally { [&] {
            auto e = error();
            action.cb(not e);
            queue.pop(1);
        }};

        if (auto const* sa = std::get_if<SetAddr>(&action.action); sa) {
            clear_error();
            i2c_instance->enable = 0;
            i2c_instance->tar = sa->addr;
            i2c_instance->enable = 1;
        }

        if (auto const* s = std::get_if<Sync>(&action.action); s) {
            while ((i2c_instance->status & I2C_IC_STATUS_TFE_BITS) == 0) {
                if (error()) {
                    goto recover;
                }
                i2c_instance->intr_mask = error_irq_mask | I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
                co_await i2c_tick;
            }
        }

        if (auto const* s = std::get_if<RecoverBus>(&action.action); s) {
            goto recover;
        }

        if (auto const* w = std::get_if<Write>(&action.action); w) {
            if (w->start_with_restart) {
                while ((i2c_instance->status & I2C_IC_STATUS_TFE_BITS) == 0) {
                    if (error()) {
                        goto recover;
                    }
                    i2c_instance->intr_mask = error_irq_mask | I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
                    co_await i2c_tick;
                }
                clear_error();
            }
            for (auto i=0U; i < w->data.size(); ++i) {
                while ((i2c_instance->status & I2C_IC_STATUS_TFNF_BITS) == 0) {
                    if (error()) {
                        goto next;
                    }
                    i2c_instance->intr_mask = error_irq_mask | I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
                    co_await i2c_tick;
                }
                std::uint32_t cmd = w->data[i];
                if (i == 0 and w->start_with_restart) {
                    cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
                }
                if ((i + 1 == w->data.size())  and (w->stop_at_end)) {
                    cmd |= I2C_IC_DATA_CMD_STOP_BITS;
                }
                i2c_instance->data_cmd = cmd;
            }
        }

        if (auto const* r = std::get_if<Read>(&action.action); r) {
            assert((i2c_instance->status & I2C_IC_STATUS_RFNE_BITS) == 0);
            if (r->start_with_restart) {
                while ((i2c_instance->status & I2C_IC_STATUS_TFE_BITS) == 0) {
                    if (error()) {
                        goto recover;
                    }
                    i2c_instance->intr_mask = error_irq_mask | I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
                    co_await i2c_tick;
                }
                clear_error();
            }
            auto rb = 0U;
            for (auto i=0U; i < r->data.size(); ++i) {
                if (error()) {
                    goto next;
                }
                while ((i2c_instance->status & I2C_IC_STATUS_TFNF_BITS) == 0) {
                    i2c_instance->intr_mask = error_irq_mask | I2C_IC_INTR_MASK_M_TX_EMPTY_BITS;
                    co_await i2c_tick;
                }
                std::uint32_t cmd = I2C_IC_DATA_CMD_CMD_BITS;
                if (i == 0 and r->start_with_restart) {
                    cmd |= I2C_IC_DATA_CMD_RESTART_BITS;
                }
                if ((i+1 == r->data.size()) and (r->stop_at_end)) {
                    cmd |= I2C_IC_DATA_CMD_STOP_BITS;
                }
                i2c_instance->data_cmd = cmd;
                while (i2c_instance->status & I2C_IC_STATUS_RFNE_BITS) {
                    auto cmd_r = i2c_instance->data_cmd;
                    r->data[rb++] = cmd_r & 0xff;
                    assert(rb <= r->data.size());
                }
            }
            while (rb < r->data.size()) {
                while ((i2c_instance->status & I2C_IC_STATUS_RFNE_BITS) == 0) {
                    if (error()) {
                        goto next;
                    }
                    i2c_instance->intr_mask = error_irq_mask | I2C_IC_INTR_MASK_M_RX_FULL_BITS;
                    co_await i2c_tick;
                }
                auto cmd_r = i2c_instance->data_cmd;
                r->data[rb++] = cmd_r & 0xff;
            }
        }
    }

    co_return;
}


#pragma GCC push_options
#pragma GCC optimize ("O0")
void i2c_irq() {
    i2c_instance->intr_mask = error_irq_mask;
    if (i2c_tick.handle) {
        i2c_tick();
    } else {
        // clear_error();
    }
}
#pragma GCC pop_options

}

void I2C::set_addr(std::uint8_t addr, CB cb) {
    auto success = queue.put(SetAddr{addr}, cb);
    assert(success);
    queue_tick();
}

void I2C::write(std::span<const std::uint8_t> data, bool stop_at_end, CB cb) {
    auto success = queue.put(Write{data, true, stop_at_end}, cb);
    assert(success);
    queue_tick();
}
void I2C::write_cont(std::span<const std::uint8_t> data, bool stop_at_end, CB cb) {
    auto success = queue.put(Write{data, false, stop_at_end}, cb);
    assert(success);
    queue_tick();
}

void I2C::read(std::span<std::uint8_t> data, bool stop_at_end, CB cb) {
    auto success = queue.put(Read{data, true, stop_at_end}, cb);
    assert(success);
    queue_tick();
}
void I2C::read_cont(std::span<std::uint8_t> data, bool stop_at_end, CB cb) {
    auto success = queue.put(Read{data, false, stop_at_end}, cb);
    assert(success);
    queue_tick();
}

void I2C::sync(CB cb) {
    auto success = queue.put(Sync{}, cb);
    assert(success);
    queue_tick();
}

void I2C::recover_bus(CB cb) {
    auto success = queue.put(RecoverBus{}, cb);
    assert(success);
    queue_tick();
}

I2C::I2C() {
    if (i2c_instance == i2c0_hw) {
        irq_set_exclusive_handler(I2C0_IRQ, i2c_irq);
        irq_set_enabled(I2C0_IRQ, true);
    } else {
        irq_set_exclusive_handler(I2C1_IRQ, i2c_irq);
        irq_set_enabled(I2C1_IRQ, true);
    }
    worker_task = work();
}
