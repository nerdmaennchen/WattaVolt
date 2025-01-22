#include "cranc/module/Module.h"
#include "usb/usb_dev.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/coro/Task.h"
#include "cranc/coro/SwitchToMainLoop.h"
#include "cranc/platform/system.h"
#include "cranc/config/ApplicationConfig.h"

#include "util/Finally.h"

#include <string.h>
#include <cstring>

#include <array>


namespace
{


using namespace usb::literals;

auto interface_name = "general interface"_usb_str;

cranc::coro::Task<void> worker_task;
cranc::coro::Awaitable<void, cranc::LockGuard> usb_rx;
cranc::coro::Awaitable<void, cranc::LockGuard> usb_tx_done;



template<typename T>
concept is_container = requires(T v) { 
    { v.begin() };
    { v.size() }; 
};

union  {
	struct __attribute__((packed)) {
		std::uint8_t operation;
		std::uint16_t index;
		std::array<std::uint8_t, 512> payload;	
	};
	std::array<std::uint8_t, sizeof(operation)+sizeof(index)+sizeof(payload)> raw;
} rx_buffer;

union  {
	struct __attribute__((packed)) {
		std::uint16_t size;
		std::array<std::uint8_t, 512> payload;	
	};
	std::array<std::uint8_t, sizeof(size)+sizeof(payload)> raw;

	std::span<const std::uint8_t> fill(auto const& data) {
		if constexpr ( requires {data.begin(); data.end(); data.size();}) {
			assert(data.size() <= payload.size());
			std::memcpy(payload.data(), data.data(), data.size());
			size = data.size();
			return {raw.data(), size+sizeof(size)};
		} else {
			static_assert(sizeof(data) <= sizeof(payload));
			std::memcpy(payload.data(), &data, sizeof(data));
			size = sizeof(data);
			return {raw.data(), size+sizeof(size)};
		}
	}

	std::span<const std::uint8_t> fill() {
		size = 0;
		return {raw.data(), size+sizeof(size)};
	}
} tx_buffer;

std::span<std::uint8_t> rx_buffer_view_free{rx_buffer.raw};
std::span<std::uint8_t> rx_buffer_view_received{rx_buffer.raw.data(), 0};

std::array<usb::endpoint, 2> eps  = {
    usb::endpoint{
        .descriptor = {
            .bEndpointAddress = USB_DIR_OUT | 1,
            .bmAttributes = USB_TRANSFER_TYPE_BULK,
            .wMaxPacketSize = 64,
            .bInterval = 1,
        },
        .double_buffered = false,
        .cb = [](std::span<std::uint8_t> data) {
			if (data.size() > rx_buffer_view_free.size()) {
				return;
			}
			std::memcpy(rx_buffer_view_free.data(), data.data(), data.size());
			rx_buffer_view_free = rx_buffer_view_free.subspan(data.size());
			rx_buffer_view_received = {rx_buffer.raw.data(), static_cast<std::size_t>(rx_buffer_view_free.data() - rx_buffer.raw.data())};
			usb_rx();
		},
    },
    usb::endpoint{
        .descriptor = {
            .bEndpointAddress = USB_DIR_IN | 1,
            .bmAttributes = USB_TRANSFER_TYPE_BULK,
            .wMaxPacketSize = 64,
            .bInterval = 1,
        },
        .double_buffered = false,
        .cb = [](std::span<std::uint8_t>){ 
			usb_tx_done(); 
		},
    },
};
auto& ep_out = eps[0];
auto& ep_in = eps[1];

std::array<usb::usb_iface_setting, 1> iface_settings {
    usb::usb_iface_setting{
        .descriptor = {
            .bInterfaceClass = 0xff,
            .bInterfaceSubClass = 0x00,
            .bInterfaceProtocol = 0x00,
			.iInterface = interface_name,
        },
        .endpoints = eps,
    },
};

usb::Interface iface {
    default_usb_dev::config,
    iface_settings
};

std::span<const std::uint8_t> response{};

cranc::coro::Task<void> worker() {
	auto& configs = cranc::util::GloballyLinkedList<cranc::ApplicationConfigBase>::getHead();
	cranc::coro::SwitchToMainLoop sw2main;
	rx_buffer_view_free = rx_buffer.raw;
	while (true) {
		usb_rx.clear();
		ep_out.start_rx();
		co_await usb_rx;

		if (rx_buffer_view_received.size() == 0) {
			co_return;
		}

		if (rx_buffer.operation == 0) {
			std::uint16_t count = 0;
			for ([[maybe_unused]] auto const& c : configs) {
				++count;
			}
			response = tx_buffer.fill(count);
		} else {
			constexpr auto header_size = sizeof(rx_buffer.operation) +  sizeof(rx_buffer.index);
			if (rx_buffer_view_received.size() < header_size) {
				co_return;
			}
			auto it = configs.begin();
			auto end = configs.end();
			for (auto i{0}; (i < rx_buffer.index) and (it != end); ++i, ++it) {}
			if (it == end) {
				co_return;
			}
			auto& tgt = *it;

			if (rx_buffer.operation == 1) {
				std::uint16_t size = tgt->getSize();
				response = tx_buffer.fill(size);
			}
			if (rx_buffer.operation == 2) {
				response = tx_buffer.fill(tgt->getName());
			}
			if (rx_buffer.operation == 3) {
				response = tx_buffer.fill(tgt->getFormat());
			}
			if (rx_buffer.operation == 4) {
				co_await sw2main;
				cranc::LockGuard lock;
				response = tx_buffer.fill(tgt->getValue());
			}
			if (rx_buffer.operation == 5) {
				auto payload_len = tgt->getSize();
				if (rx_buffer.payload.size() < payload_len) {
					co_return;
				}
				while (rx_buffer_view_received.size() < header_size + payload_len) {
					ep_out.start_rx();
					co_await usb_rx;
				}
				co_await sw2main;
				cranc::LockGuard lock;
				tgt->setValue(std::span<std::uint8_t>{rx_buffer.payload.data(), payload_len});
				response = tx_buffer.fill();
			}
		}

		rx_buffer_view_free = rx_buffer.raw;
		rx_buffer_view_received = {rx_buffer.raw.data(), 0};

		while (not response.empty()) {
			usb_tx_done.clear();
			auto sent = ep_in.send(response);
			response = response.subspan(sent);
			co_await usb_tx_done;
		}
	}
}

struct USBComModule : cranc::Module {

	using cranc::Module::Module;

	void init() override {
        iface.on_altsetting_changed = []() {
			cranc::LockGuard lock;
			worker_task.terminate();
            if (not iface.cur_active_altsetting.has_value()) {
                return;
            }
			worker_task = worker();
		};
	}

} _{1000};

}
