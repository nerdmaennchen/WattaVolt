#include "analog_readings.h"
#include "output.h"

#include "cranc/module/Module.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/coro/SwitchToMainLoop.h"

#include "cranc/timer/ISRTime.h"
#include "cranc/timer/swTimer.h"

#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>

#include "lvgl.h"

#include <format>

namespace {
using namespace std::literals::chrono_literals;

auto spi = spi0;

constexpr auto bitrate = 10'000'000;

constexpr auto spi_sck = 22;
constexpr auto spi_tx  = 23;
constexpr auto spi_ncs = 26;

constexpr auto rst_pin = 25;
constexpr auto dc_pin  = 24;

constexpr auto led_pin  = 27;

constexpr auto pixels_x = 160;
constexpr auto pixels_y = 128;

std::uint32_t tx_dma;
std::uint32_t rx_dma;

dma_channel_config tx_dma_config;
dma_channel_config rx_dma_config;

std::uint8_t rx_dma_dummy;

lv_display_t *lcd_disp{};

std::unique_ptr<lv_color_t[]> buf1, buf2;

volatile bool dma_busy {false};

void xfer_done_handler()
{
    cranc::ISRTime isrTimer;

    gpio_set_mask(1 << spi_ncs);
    auto ints = dma_hw->ints1;
    dma_hw->intf1 = 0;
    dma_hw->ints1 = (1 << rx_dma);
    dma_busy = false;
    lv_display_flush_ready(lcd_disp);
}

void lcd_send_cmd(lv_display_t *, const uint8_t *cmd, size_t cmd_size, const uint8_t *param, size_t param_size)
{
    while (dma_busy);
    gpio_clr_mask(1 << dc_pin);
    gpio_clr_mask(1 << spi_ncs);
    
    spi_write_blocking(spi, cmd, cmd_size);
    gpio_set_mask(1 << dc_pin);
    spi_write_blocking(spi, param, param_size);
    gpio_set_mask(1 << spi_ncs);
}

void lcd_send_color(lv_display_t *, const uint8_t *cmd, size_t cmd_size, uint8_t *param, size_t param_size)
{
    while (dma_busy);

    dma_busy = true;

    gpio_clr_mask(1 << dc_pin);
    gpio_clr_mask(1 << spi_ncs);
    
    spi_write_blocking(spi, cmd, cmd_size);
    gpio_set_mask(1 << dc_pin);

    dma_channel_set_read_addr(tx_dma, param, false);

    dma_channel_set_trans_count(rx_dma, param_size, true);
    dma_channel_set_trans_count(tx_dma, param_size, true);
}

lv_style_t style_channel;
lv_style_t small;
lv_style_t number;

struct Label {
    Label(lv_obj_t * parent, char const* text, lv_style_t const* style) {
        label = lv_label_create(parent);
        lv_label_set_text(label, text);
        if (style) {
            lv_obj_add_style(label, style, LV_STATE_DEFAULT);
        }
    }
    ~Label() {
        lv_obj_del(label);
    }
    operator lv_obj_t *() { return label; }
private:
    lv_obj_t *label;
};

const int32_t ch_col_dsc[] = {1, 1, 1, LV_GRID_FR(50), 1, LV_GRID_FR(50), LV_GRID_TEMPLATE_LAST};
const int32_t ch_row_dsc[] = {LV_GRID_FR(50), 3, LV_GRID_FR(50), 1, LV_GRID_TEMPLATE_LAST};
struct Channel {
    Channel(lv_obj_t * parent, char const* name) 
    : l_name{parent, name, nullptr}
    , l_u{parent, "U", &small}
    , l_i{parent, "I", &small}
    , l_s1{parent, "/", &small}
    , l_s2{parent, "/", &small}
    , l_u_set{parent, "?", &number}
    , l_i_set{parent, "?", &number}
    , l_u_cur{parent, "?", &number}
    , l_i_cur{parent, "?", &number}
    {
        lv_obj_add_style(parent, &style_channel, LV_STATE_DEFAULT);
        lv_obj_set_layout(parent, LV_LAYOUT_GRID);
        lv_obj_set_grid_dsc_array(parent, ch_col_dsc, ch_row_dsc);

        lv_obj_set_grid_cell(l_name, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 3);
        lv_obj_set_grid_cell(l_u, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(l_i, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 2, 1);
        lv_obj_set_grid_cell(l_s1, LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(l_s2, LV_GRID_ALIGN_CENTER, 4, 1, LV_GRID_ALIGN_CENTER, 2, 1);

        lv_obj_set_grid_cell(l_u_set, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 0, 1);
        lv_obj_set_grid_cell(l_u_cur, LV_GRID_ALIGN_END, 5, 1, LV_GRID_ALIGN_CENTER, 0, 1);

        lv_obj_set_grid_cell(l_i_set, LV_GRID_ALIGN_END, 3, 1, LV_GRID_ALIGN_CENTER, 2, 1);
        lv_obj_set_grid_cell(l_i_cur, LV_GRID_ALIGN_END, 5, 1, LV_GRID_ALIGN_CENTER, 2, 1);
    }

    void update_cur(float voltage, float current) {
        auto volt_str = std::format("{:7.2f}V", voltage);
        auto cur_str  = std::format("{:7.2f}A", current);
        lv_label_set_text(l_u_cur, volt_str.c_str());
        lv_label_set_text(l_i_cur, cur_str.c_str());
    }

    void update_sp(float voltage, float current) {
        auto volt_str = std::format("{:7.2f}V", voltage);
        auto cur_str  = std::format("{:7.2f}A", current);
        lv_label_set_text(l_u_set, volt_str.c_str());
        lv_label_set_text(l_i_set, cur_str.c_str());
    }

    Label l_name;
    Label l_u;
    Label l_i;
    Label l_s1, l_s2;

    Label l_u_set;
    Label l_i_set;
    Label l_u_cur;
    Label l_i_cur;
};

struct : cranc::Module {
    using cranc::Module::Module;

    cranc::coro::Task<void> disp_task, monitor_task;

    Channel* ch1{};
    Channel* ch2{};

    AnalogReadings readings{};
    cranc::Listener<AnalogReadings> listener_readings{[&](AnalogReadings const& r) { 
        readings = r;
        if (ch1) {
            ch1->update_cur(r.u0, r.i0);
        }
        if (ch2) {
            ch2->update_cur(r.u1, r.i1);
        }
    }};
    OutputSetpoint setpoint{};
    cranc::Listener<OutputSetpoint> listener_setpoint{[&](OutputSetpoint const& s) { 
        setpoint = s;
        if (ch1) {
            ch1->update_sp(s.u0, s.i0);
        }
        if (ch2) {
            ch2->update_sp(s.u1, s.i1);
        }
    }};

    cranc::coro::Task<void> monitor() {
        auto root = lv_screen_active();
        lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_text_color(root, lv_color_hex(0xffffff), LV_PART_MAIN);

        lv_style_init(&style_channel);
        lv_style_set_bg_color(&style_channel, lv_color_hex(0x000000));
        lv_style_set_border_width(&style_channel, 1);
        lv_style_set_radius(&style_channel, 3);
        lv_style_set_border_color(&style_channel, lv_color_hex(0x111111));

        lv_style_init(&small);
        lv_style_set_text_font(&small, &lv_font_montserrat_14);
        lv_style_init(&number);
        lv_style_set_text_font(&number, &lv_font_unscii_8);

        auto* top = lv_obj_create(root);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_size(top, pixels_x, pixels_y / 2);

        auto* bot = lv_obj_create(root);
        lv_obj_set_pos(bot, 0, pixels_y / 2);
        lv_obj_set_size(bot, pixels_x, pixels_y / 2);

        Channel ch1_{top, "0"};
        Channel ch2_{bot, "1"};

        ch1_.update_cur(readings.u0, readings.i0);
        ch2_.update_cur(readings.u1, readings.i1);
        ch1_.update_sp(setpoint.u0, setpoint.i0);
        ch2_.update_sp(setpoint.u1, setpoint.i1);

        ch1 = &ch1_;
        ch2 = &ch2_;

        co_await std::suspend_always{};
    }

    cranc::coro::Task<void> display() {
        cranc::coro::SwitchToMainLoop sw2main{};
        spi_init(spi, bitrate);
        gpio_set_function(spi_sck, GPIO_FUNC_SPI);
        gpio_set_function(spi_tx,  GPIO_FUNC_SPI);

        for (auto pin : {spi_ncs, rst_pin, dc_pin, led_pin}) {
            gpio_set_function(pin, GPIO_FUNC_SIO);
            gpio_set_dir(pin, GPIO_OUT);
        }
        gpio_set_mask((1 << spi_ncs) | (1 << rst_pin) | (1 << dc_pin) | (1 << led_pin));

        gpio_clr_mask(1 << rst_pin);
        co_await cranc::coro::AwaitableDelay{10ms};
        gpio_set_mask(1 << rst_pin);
        co_await cranc::coro::AwaitableDelay{10ms};

        co_await sw2main;

        tx_dma = dma_claim_unused_channel(true);
        tx_dma_config = dma_channel_get_default_config(tx_dma);
        channel_config_set_transfer_data_size(&tx_dma_config, DMA_SIZE_8);
        channel_config_set_dreq(&tx_dma_config, spi_get_dreq(spi, true));
        channel_config_set_read_increment(&tx_dma_config, true);
        channel_config_set_write_increment(&tx_dma_config, false);
        dma_channel_configure(tx_dma, &tx_dma_config, &spi_get_hw(spi)->dr,
            nullptr, 0, false);
            
        rx_dma = dma_claim_unused_channel(true);
        rx_dma_config = dma_channel_get_default_config(rx_dma);
        channel_config_set_transfer_data_size(&rx_dma_config, DMA_SIZE_8);
        channel_config_set_dreq(&rx_dma_config, spi_get_dreq(spi, false));
        channel_config_set_read_increment(&rx_dma_config, false);
        channel_config_set_write_increment(&rx_dma_config, false);
        dma_channel_configure(rx_dma, &rx_dma_config, &rx_dma_dummy,
            &spi_get_hw(spi)->dr, 0, false);

        dma_channel_set_irq1_enabled(rx_dma, true);
        irq_set_exclusive_handler(DMA_IRQ_1, xfer_done_handler);
        irq_set_enabled(DMA_IRQ_1, true);

        lv_init();
        lv_tick_set_cb([]() -> uint32_t {
            auto now = cranc::getSystemTime();
            return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        });

        lcd_disp = lv_st7735_create(pixels_y, pixels_x, LV_LCD_FLAG_BGR, lcd_send_cmd, lcd_send_color);
        lv_display_set_rotation(lcd_disp, LV_DISPLAY_ROTATION_90);
        
        uint32_t buf_size = pixels_x * pixels_y * lv_color_format_get_size(lv_display_get_color_format(lcd_disp)) / 8;
        buf1 = std::make_unique<lv_color_t[]>(buf_size);
        buf2 = std::make_unique<lv_color_t[]>(buf_size);
        
        lv_display_set_buffers(lcd_disp, buf1.get(), buf2.get(), buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        
        monitor_task = monitor();

        cranc::TimePoint last_call_ts{};
        auto ticker = cranc::coro::AwaitableDelay{cranc::getSystemTime(), 10ms};
        while (true) {
            co_await ticker;
            co_await sw2main;
            auto now = cranc::getSystemTime();
            auto delta = now - last_call_ts;
            lv_tick_inc(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count());
            last_call_ts = now;
            lv_timer_handler();
        }
    }

    void init() override
    {
        disp_task = display();
    }
} _{1000};

}
