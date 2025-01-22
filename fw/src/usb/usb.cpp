#include "usb.h"
#include "usb_dev.h"

#include "cranc/module/Module.h"
#include "cranc/timer/swTimer.h"
#include "cranc/timer/systemTime.h"
#include "cranc/timer/ISRTime.h"

#include "cranc/coro/Task.h"
#include "cranc/coro/Generator.h"
#include "cranc/coro/Awaitable.h"
#include "cranc/coro/SwitchToMainLoop.h"

#include "util/Finally.h"
#include "util/span_helpers.h"
#include "util/ChunkBuffer.h"

#include "dev_lowlevel.h"

#include "hardware/regs/usb.h"
#include "hardware/structs/usb.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "pico/unique_id.h"
#include "rp2040_usb_device_enumeration.h"

#include "pico/stdlib.h"

#include <string.h>
#include <array>
#include <span>
#include <cstring>
#include <cassert>

#define usb_hw_set hw_set_alias(usb_hw)
#define usb_hw_clear hw_clear_alias(usb_hw)

namespace usb{
namespace
{

using namespace std::chrono_literals;
using namespace usb::literals;

const auto manufacturer_str = "Lutzi"_usb_str;
const auto      product_str = "my_psu"_usb_str;
      auto       serial_str = "unknown"_usb_str;


usb_device_descriptor device_descriptor {
	.bLength = sizeof(usb_device_descriptor),
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0110,
	.bDeviceClass    = 0xef, // Miscellaneous Device
	.bDeviceSubClass = 2,    // whatever
	.bDeviceProtocol = 1,    // Interface Association
	.bMaxPacketSize0 = 64,
	.idVendor = 0xFFFF,
	.idProduct = 0x1234,
	.bcdDevice = 0x0200,
	.iManufacturer = 0,
	.iProduct = 0,
	.iSerialNumber = 0,
	.bNumConfigurations = 0,
};

void ep0_in_handler(std::span<std::uint8_t>);

template<std::size_t N>
std::size_t ceilN(std::size_t num) {
	return N * ((num + N-1) / N);
}

std::size_t ceil64(std::size_t num) {
	return ceilN<64>(num);
}

std::size_t round_2_pow(std::size_t num) {
	std::uint32_t i = 1;
	while(i < num) {
		i <<= 1;
	}
	return i;
}

template<std::size_t chunk_size>
cranc::coro::Generator<std::span<const std::uint8_t>> chunkify(std::span<const std::uint8_t> data){
	ChunkBuffer<chunk_size> buffer;

	if (data.empty()) {
		co_yield buffer.flush();
	}

	while (not data.empty()) {
		buffer.push(data);
		if (buffer.full()) {
			co_yield buffer.flush();
		}
	}
	if (not buffer.empty()) {
		co_yield buffer.flush();
	}
}

struct usb_endpoint_configuration {
    endpoint const *descriptor;

    // Pointers to endpoint + buffer control registers
    // in the USB controller DPSRAM
    volatile std::uint32_t *endpoint_control;
    volatile std::uint32_t *buffer_control;
    std::span<uint8_t> data_buffer;
    std::span<uint8_t> data_buffer0;
    std::span<uint8_t> data_buffer1;

    // Toggle after each packet (unless replying to a SETUP)
    std::uint8_t next_pid;

	std::uint32_t db_iso_offset;
};

std::array<usb_endpoint_configuration, 16> out_ep_configurations{};
std::array<usb_endpoint_configuration, 16> in_ep_configurations{};

usb_endpoint_configuration& config_for_ep(std::uint8_t ep_addr) {
	auto dir_in = ep_addr & USB_DIR_IN;
	ep_addr &= ~USB_DIR_IN;
	if (dir_in) {
		assert(ep_addr < in_ep_configurations.size());
		return in_ep_configurations[ep_addr];
	} else {
		assert(ep_addr < out_ep_configurations.size());
		return out_ep_configurations[ep_addr];
	}
}

void stall_ep(usb_endpoint_configuration& config, bool stalled) {
	auto reg = *config.buffer_control;
	if (stalled) {
		reg |= USB_BUF_CTRL_STALL;
	} else {
		reg &= ~USB_BUF_CTRL_STALL;
	}
    *config.buffer_control = reg;
}

bool stall_ep_get(usb_endpoint_configuration& config) {
	auto reg = *config.buffer_control;
	return reg & USB_BUF_CTRL_STALL;
}


auto& ep_in_config = in_ep_configurations[0];
auto& ep_out_config = out_ep_configurations[0];

Configuration* cur_active_config{};


Interface* interface_by_index(std::uint8_t idx) {
	for (auto& iface : cur_active_config->ifaces) {
		if (idx == 0) {
			return iface;
		}
		--idx;
	}
	return {};
}

static void usb_12_cycle_delay() {
	__asm volatile (
			"b 1f\n"
			"1: b 1f\n"
			"1: b 1f\n"
			"1: b 1f\n"
			"1: b 1f\n"
			"1: b 1f\n"
			"1:\n"
			: : : "memory");
}

std::span<std::uint8_t> get_buffer(usb_endpoint_configuration& ep) {
	assert(ep.descriptor);
	assert(USB_DIR_IN == (ep.descriptor->descriptor.bEndpointAddress & USB_DIR_IN));
	if (0 == (*ep.buffer_control & USB_BUF_CTRL_AVAIL)) {
		return ep.data_buffer0;
	}
	if (ep.descriptor->double_buffered) {
		if (0 == (*ep.buffer_control & (USB_BUF_CTRL_AVAIL << 16))) {
			return ep.data_buffer1;
		}
	}
	return {};
}

void flush_prefilled_buffer(usb_endpoint_configuration& ep, std::size_t size, bool buf1=false) {
	assert(ep.descriptor);
	assert(USB_DIR_IN == (ep.descriptor->descriptor.bEndpointAddress & USB_DIR_IN));

	std::uint32_t shift = buf1?16:0;
	std::uint32_t buf_ctl_other_buf = (*ep.buffer_control) & (0xffff << (16-shift));

    std::uint32_t buf_ctl = size | USB_BUF_CTRL_FULL | USB_BUF_CTRL_LAST;
    buf_ctl |= ep.next_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;
    
	ep.next_pid ^= 1u;

	*ep.buffer_control = buf_ctl_other_buf | ((buf_ctl) << shift); 
	usb_12_cycle_delay();
	*ep.buffer_control = buf_ctl_other_buf | ((buf_ctl | USB_BUF_CTRL_AVAIL) << shift); 
}


std::size_t tx_data(usb_endpoint_configuration& ep, std::span<const std::uint8_t> data) {
	assert(ep.descriptor);
	assert(USB_DIR_IN == (ep.descriptor->descriptor.bEndpointAddress & USB_DIR_IN));

	if (data.data() == ep.data_buffer0.data()) {
		assert(data.size() <= ep.data_buffer0.size());
		flush_prefilled_buffer(ep, data.size(), false);
		return data.size();
	}
	if (ep.data_buffer1.data() and (data.data() == ep.data_buffer1.data())) {
		assert(data.size() <= ep.data_buffer1.size());
		flush_prefilled_buffer(ep, data.size(), true);
		return data.size();
	}
	auto buffer = get_buffer(ep);
	data = trim_span(data, buffer.size());
	memcpy((void *)buffer.data(), data.data(), data.size());
	flush_prefilled_buffer(ep, data.size(), buffer.data() == ep.data_buffer1.data());
	return data.size();
}

void start_rx(usb_endpoint_configuration& ep) {
	assert(ep.descriptor);
	assert(USB_DIR_OUT == (ep.descriptor->descriptor.bEndpointAddress & USB_DIR_OUT));
	auto n_pid = ep.next_pid;

    uint32_t buf_ctl = ep.descriptor->descriptor.wMaxPacketSize | USB_BUF_CTRL_AVAIL;
    buf_ctl |= n_pid ? USB_BUF_CTRL_DATA1_PID : USB_BUF_CTRL_DATA0_PID;

	auto ctl = *ep.buffer_control;
	if (not ep.descriptor->double_buffered or not n_pid) {
		if ((ctl & USB_BUF_CTRL_AVAIL) == USB_BUF_CTRL_AVAIL) {
			return; // already started
		}
    	*ep.buffer_control = (ctl & (0xffff << 16)) | (buf_ctl << 0);
	} else {
		if ((ctl & (USB_BUF_CTRL_AVAIL << 16)) == (USB_BUF_CTRL_AVAIL << 16)) {
			return; // already started
		}
    	*ep.buffer_control = (ctl & (0xffff << 0)) | (buf_ctl << 16) | ep.db_iso_offset;
	}
    ep.next_pid ^= 1u;
}

void stop_rx(usb_endpoint_configuration& ep) {
	assert(ep.descriptor);
	assert(USB_DIR_OUT == (ep.descriptor->descriptor.bEndpointAddress & USB_DIR_OUT));
	
	*ep.buffer_control = 0;
}

std::span<std::uint8_t const> read_data(usb_endpoint_configuration& ep) {
	if (not ep.descriptor) {
		return {};
	}
	if (USB_DIR_OUT != ep.descriptor->descriptor.bEndpointAddress & USB_DIR_OUT) {
		return {};
	}
    std::size_t len = *ep.buffer_control & USB_BUF_CTRL_LEN_MASK;
	return ep.data_buffer0.subspan(0, len);
}

bool needs_zlp(std::size_t data_len, std::size_t req_len, uint8_t ep_size)
{
	if (data_len < req_len) {
		return data_len && (data_len % ep_size == 0);
	}
	return false;
}

cranc::coro::Task<void> control_task;
cranc::coro::Awaitable<void, cranc::LockGuard> on_setup_packet;
cranc::coro::Awaitable<void, cranc::LockGuard> ep0_tx_done;
cranc::coro::Awaitable<std::span<const std::uint8_t>, cranc::LockGuard> ep0_rx_done;

const endpoint ep0_out_desc {
	.descriptor = {
		.bLength          = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType  = USB_DT_ENDPOINT,
		.bEndpointAddress = USB_DIR_OUT | 0,
		.bmAttributes     = USB_TRANSFER_TYPE_CONTROL,
		.wMaxPacketSize   = 64,
		.bInterval        = 0
	},
	.cb = [](auto data) { ep0_rx_done(data); }
};
const endpoint ep0_in_desc {
	.descriptor = {
		.bLength          = sizeof(struct usb_endpoint_descriptor),
		.bDescriptorType  = USB_DT_ENDPOINT,
		.bEndpointAddress = USB_DIR_IN | 0,
		.bmAttributes     = USB_TRANSFER_TYPE_CONTROL,
		.wMaxPacketSize   = 64,
		.bInterval        = 0
	},
	.cb = [](auto) { ep0_tx_done(); }
};

bool disable_altsetting(usb::Interface& iface) {
	if (not iface->cur_active_altsetting) {
		return false;
	}

	auto& setting = iface->altsettings[*iface->cur_active_altsetting];
	// disable all the endpoints for this altsetting
	for (auto& ep : setting.endpoints) {
		auto& config = config_for_ep(ep.descriptor.bEndpointAddress);
		*config.endpoint_control = 0;
	}
	iface->cur_active_altsetting.reset();
	iface->on_altsetting_changed();

	return true;
}

bool enable_altsetting(usb::Interface& iface, std::uint8_t alt_setting_no) {
	if (alt_setting_no >= iface->altsettings.size()) {
		return false;
	}

	if (iface->cur_active_altsetting) {
		auto& setting = iface->altsettings[*iface->cur_active_altsetting];
		// disable all the endpoints for this altsetting
		for (auto& ep : setting.endpoints) {
			auto& config = config_for_ep(ep.descriptor.bEndpointAddress);
			
			*config.endpoint_control = 0;
			config.data_buffer0 = {};
			config.data_buffer1 = {};
		}
	}

	iface->cur_active_altsetting.emplace(alt_setting_no);
	{
		auto& setting = iface->altsettings[*iface->cur_active_altsetting];
		// enable all the endpoints for this altsetting
		for (auto& ep : setting.endpoints) {
			auto& config = config_for_ep(ep.descriptor.bEndpointAddress);
			std::size_t offset =  config.data_buffer.data() - reinterpret_cast<std::uint8_t*>(usb_dpram);
			config.descriptor = &ep;
			*config.buffer_control = 0;
			config.next_pid = 0;
			config.data_buffer0 = config.data_buffer;
			config.data_buffer1 = {};
			uint32_t reg = EP_CTRL_ENABLE_BITS
						| EP_CTRL_INTERRUPT_PER_BUFFER
						| (ep.descriptor.bmAttributes << EP_CTRL_BUFFER_TYPE_LSB)
						| offset;

			if (ep.double_buffered) {
				auto pkt_size = config.descriptor->descriptor.wMaxPacketSize;
				reg |= EP_CTRL_DOUBLE_BUFFERED_BITS;

				auto pkt_buf_size = ceil64(pkt_size);
				if (ep.descriptor.bmAttributes == 1) { // isochronous transfers only
					pkt_buf_size = std::max<std::size_t>(128, round_2_pow(pkt_buf_size));
					assert(config.data_buffer.size() >= pkt_buf_size * 2);
					config.data_buffer0 = config.data_buffer.subspan(0, pkt_buf_size);
					config.data_buffer1 = config.data_buffer.subspan(pkt_buf_size, pkt_buf_size);
					if (pkt_buf_size == 128) {
						config.db_iso_offset = 0 << 27;
					} else if (pkt_buf_size == 256) {
						config.db_iso_offset = 1 << 27;
					} else if (pkt_buf_size == 512) {
						config.db_iso_offset = 2 << 27;
					} else if (pkt_buf_size == 1024) {
						config.db_iso_offset = 3 << 27;
					} else {
						assert(false);
					}
				} else {
					assert(config.data_buffer.size() >= pkt_buf_size * 2);
					config.data_buffer0 = config.data_buffer.subspan(0, pkt_buf_size);
					config.data_buffer1 = config.data_buffer.subspan(pkt_buf_size, pkt_buf_size);
				}
			}
			*config.endpoint_control = reg;
		}
		iface->on_altsetting_changed();
	}
	return true;
}

namespace dev_request {

cranc::coro::Task<void> set_dev_addr(std::uint8_t dev_addr) {
	static std::uint8_t tgt_addr;
	tgt_addr = dev_addr;
	ep0_tx_done.clear();
	flush_prefilled_buffer(ep_in_config, 0);
	co_await ep0_tx_done;
	usb_hw->dev_addr_ctrl = dev_addr;
}

cranc::coro::Task<void> get_status(control_data const& pkt) {
	ep_in_config.data_buffer0[0] = 0;
	ep_in_config.data_buffer0[1] = 0;
	ep0_tx_done.clear();
	flush_prefilled_buffer(ep_in_config, 2);
	co_await ep0_tx_done;
}

cranc::coro::Task<void> send_device_descriptor(control_data pkt) {
	device_descriptor.bNumConfigurations = Configuration::getHead().count();
	device_descriptor.iManufacturer = manufacturer_str.index();
	device_descriptor.iProduct = product_str.index();
	device_descriptor.iSerialNumber = serial_str.index();

	auto to_send = trim_span(to_span(device_descriptor), pkt.setup_pkt.wLength);
	auto c = chunkify<64>(to_send);
	while (c.advance()) {
		auto chunk = c.get();
		tx_data(ep_in_config, chunk);
		co_await ep0_tx_done;
	}
}


cranc::coro::Task<void> send_config_descriptor(control_data pkt) {
	auto chunkifier = [](std::size_t total_len, std::uint8_t idx) -> cranc::coro::Generator<std::span<std::uint8_t const>> {
		ChunkBuffer<64> buffer;
		bool zlp = false;

		Configuration* cfg{};
		{
			auto i = idx;
			for (auto& config : Configuration::getHead()) {
				if (i == 0) {
					cfg = config;
					break;
				}
				--i;
			}
			if (not cfg) {
				co_return;
			}
		}

		{ // the config descriptor
			auto calc_total_len = [&]() {
				std::uint16_t total_len = sizeof(usb_configuration_descriptor);
				for (auto& iface_ass : cfg->iface_associations) {
					total_len += sizeof(iface_ass->descriptor);
				}
				for (auto& iface : cfg->ifaces) {
					for (auto const& altsetting : iface->altsettings) {
						total_len += sizeof(altsetting.descriptor);
						total_len += altsetting.extra.size();

						for (auto const& ep : altsetting.endpoints) {
							total_len += sizeof(ep.descriptor);
							total_len += ep.extra.size();
						}
					}
				}
				return total_len;
			};

			const usb_configuration_descriptor descriptor {
				.wTotalLength = calc_total_len(),
				.bNumInterfaces = static_cast<std::uint8_t>(cfg->ifaces.count()),
				.bConfigurationValue = static_cast<std::uint8_t>(idx+1),
				.iConfiguration = cfg->_iConfig?cfg->_iConfig->index():std::uint8_t{0},
				.bmAttributes = cfg->_bmAttributes,
				.bMaxPower = cfg->_bMaxPower,
			};

			zlp = needs_zlp(descriptor.wTotalLength, total_len, 64);

			auto descriptor_span = trim_span(to_span_c(descriptor), total_len);
			total_len -= descriptor_span.size();

			while (not descriptor_span.empty()) {
				buffer.push(descriptor_span);
				if (buffer.full()) {
					co_yield buffer.flush();
				}
			}
		}
		// the interface associations
		{
			for (auto& iface_ass : cfg->iface_associations) {
				auto descriptor_span = trim_span(to_span_c(iface_ass->descriptor), total_len);
				total_len -= descriptor_span.size();

				while (not descriptor_span.empty()) {
					buffer.push(descriptor_span);
					if (buffer.full()) {
						co_yield buffer.flush();
					}
				}
			}
		}
		// the interfaces
		{
			for (auto& iface : cfg->ifaces) {
				std::uint8_t alt_setting_no = 0;
				auto iface_index = iface->index();
				for (auto& setting : iface->altsettings) {
					auto increment_alt_setting = Finally{[&]{++alt_setting_no;}};
					{
						auto descriptor = setting.descriptor;
						descriptor.bInterfaceNumber = iface_index;
						descriptor.bAlternateSetting = alt_setting_no;
						descriptor.bNumEndpoints = setting.endpoints.size();

						auto descriptor_span = trim_span(to_span_c(descriptor), total_len);
						total_len -= descriptor_span.size();

						while (not descriptor_span.empty()) {
							buffer.push(descriptor_span);
							if (buffer.full()) {
								co_yield buffer.flush();
							}
						}

						descriptor_span = trim_span(setting.extra, total_len);
						total_len -= descriptor_span.size();

						while (not descriptor_span.empty()) {
							buffer.push(descriptor_span);
							if (buffer.full()) {
								co_yield buffer.flush();
							}
						}
					}

					for (auto ep : setting.endpoints) {
						auto descriptor = ep.descriptor;
						descriptor.bDescriptorType = USB_DT_ENDPOINT;
						descriptor.bLength = sizeof(usb_endpoint_descriptor) + ep.extra.size();

						auto descriptor_span = trim_span(to_span_c(descriptor), total_len);
						total_len -= descriptor_span.size();

						while (not descriptor_span.empty()) {

							buffer.push(descriptor_span);
							if (buffer.full()) {
								co_yield buffer.flush();
							}
						}

						descriptor_span = trim_span(ep.extra, total_len);
						total_len -= descriptor_span.size();

						while (not descriptor_span.empty()) {

							buffer.push(descriptor_span);
							if (buffer.full()) {
								co_yield buffer.flush();
							}
						}
					}
				}
			}
		}

		if (not buffer.empty()) {
			co_yield buffer.flush();
		}

		if (zlp) {
			co_yield std::span<std::uint8_t const>{};
		}

	}(pkt.setup_pkt.wLength, pkt.setup_pkt.wValue & 0xff);

	while (chunkifier.advance()) {
		auto chunk = chunkifier.get();
		tx_data(ep_in_config, chunk);
		co_await ep0_tx_done;
	}

}

cranc::coro::Task<void> send_string_descriptor(control_data pkt) {
	auto index = pkt.setup_pkt.wValue & 0xff;
	if (index == 0) {
		std::array<std::uint8_t, 4> lang_id_descriptor = {
			0x04, 0x03,
			0x09, 0x04
		};
		tx_data(ep_in_config, lang_id_descriptor);
		co_await ep0_tx_done;
	} else {
		for (auto& usb_string : USB_String::getHead()) {
			index--;
			if (index == 0) {
				auto descriptor = usb_string->as_descriptor();
				auto zlp = needs_zlp(descriptor.size(), pkt.setup_pkt.wLength, ep_in_config.descriptor->descriptor.wMaxPacketSize);
				auto c = chunkify<64>(trim_span(descriptor, pkt.setup_pkt.wLength));

				while (c.advance()) {
					auto chunk = c.get();
					tx_data(ep_in_config, chunk);
					co_await ep0_tx_done;
				}
				if (zlp) {
					tx_data(ep_in_config, {});
					co_await ep0_tx_done;
				}
			}
		}
	}
}


cranc::coro::Task<void> set_device_configuration(control_data pkt) {
	if (cur_active_config) {
		for (auto& iface : cur_active_config->ifaces) {
			disable_altsetting(*iface);
		}
		cur_active_config->on_set_active(false);
	}

	for (auto i=1; i < out_ep_configurations.size(); ++i) {
		auto& config = out_ep_configurations[i];
		*config.endpoint_control = 0;
		*config.buffer_control   = 0;
		config.next_pid = 0;
	}
	for (auto i=1; i < in_ep_configurations.size(); ++i) {
		auto& config = in_ep_configurations[i];
		*config.endpoint_control = 0;
		*config.buffer_control   = 0;
		config.next_pid = 0;
	}


	cur_active_config = {};
	auto idx = pkt.setup_pkt.wValue & 0xff;
	if (idx >= 0) {
		for (auto& config : Configuration::getHead()) {
			--idx;
			if (idx == 0) {
				cur_active_config = config;
				break;
			}
		}
	}

	if (not cur_active_config) {
		return {};
	}
	
	// allocate the memory for all the endpoints
	auto find_max_packet_size = [](int ep_addr) {
		std::size_t max_size = 0;
		const endpoint* ret{};
		for (auto& iface : cur_active_config->ifaces) {
			for (auto& setting : iface->altsettings) {
				for (auto& ep : setting.endpoints) {
					if (ep.descriptor.bEndpointAddress != ep_addr) {
						continue;
					}
					// round s up to multiples of 64 bytes
					std::size_t cur_size = ceil64(ep.descriptor.wMaxPacketSize);
					if (ep.descriptor.bmAttributes == USB_TRANSFER_TYPE_ISOCHRONOUS) {
						cur_size = round_2_pow(cur_size);
						cur_size = std::max<std::size_t>(128, cur_size);
					}
					if (ep.double_buffered) {
						cur_size *= 2;
					}
					max_size = std::max(cur_size, max_size);
				}
			}
		}
		return max_size;
	};
	std::span<std::uint8_t> remaining_buffer = usb_dpram->epx_data;
	for (auto i = 1; i < in_ep_configurations.size(); ++i) {
		auto s = find_max_packet_size(i | USB_DIR_IN);
		assert(remaining_buffer.size() >= s);
		auto& config = in_ep_configurations[i];
		config.data_buffer = remaining_buffer.subspan(0, s);
		remaining_buffer = remaining_buffer.subspan(s);
	}
	for (auto i = 1; i < out_ep_configurations.size(); ++i) {
		auto s = find_max_packet_size(i | USB_DIR_OUT);
		assert(remaining_buffer.size() >= s);
		auto& config = out_ep_configurations[i];
		config.data_buffer = remaining_buffer.subspan(0, s);
		remaining_buffer = remaining_buffer.subspan(s);
	}

	// enable all the default altsettings 
	{
		for (auto& iface : cur_active_config->ifaces) {
			enable_altsetting(*iface, 0);
		}
	}

	cur_active_config->on_set_active(true);

	return [=]() -> cranc::coro::Task<void> {
		tx_data(ep_in_config, {});
		co_await ep0_tx_done;
	}();
}


cranc::coro::Task<void> get_device_configuration(control_data const& pkt) {
	ep_in_config.data_buffer0[0] = cur_active_config?cur_active_config->index()+1:0;
	ep0_tx_done.clear();
	flush_prefilled_buffer(ep_in_config, 1);
	co_await ep0_tx_done;
}

cranc::coro::Task<void> handle_get_descriptor(control_data const& pkt) {
	auto descriptor_type = (pkt.setup_pkt.wValue >> 8) & 0xff;
	switch (descriptor_type) {
	case USB_DT_DEVICE:
		return send_device_descriptor(pkt);
	case USB_DT_CONFIG:
		return send_config_descriptor(pkt);
	case USB_DT_STRING:
		return send_string_descriptor(pkt);
	default: 
		break;
	}
	return {};
}

cranc::coro::Task<void> handle_request(control_data const& pkt) {
	std::uint8_t req_type = pkt.setup_pkt.bmRequestType & USB_REQ_TYPE_TYPE_MASK;
	if (req_type != USB_REQ_TYPE_STANDARD) {
		return {}; // here we only support standard requests
	}

	auto req_dir_in = pkt.setup_pkt.bmRequestType & USB_DIR_IN;
	switch (pkt.setup_pkt.bRequest) {
	case USB_REQUEST_CLEAR_FEATURE:
	case USB_REQUEST_SET_FEATURE:
		break;
	case USB_REQUEST_SET_ADDRESS:
		if (req_dir_in) return {};
		return set_dev_addr(pkt.setup_pkt.wValue & 0x7f);
	case USB_REQUEST_SET_CONFIGURATION:
		if (req_dir_in) return {};
		return set_device_configuration(pkt);
	case USB_REQUEST_GET_CONFIGURATION:
		if (not req_dir_in) return {};
		return get_device_configuration(pkt);
	case USB_REQUEST_GET_DESCRIPTOR:
		if (not req_dir_in) return {};
		return handle_get_descriptor(pkt);
	case USB_REQUEST_GET_STATUS:
		if (not req_dir_in) return {};
		return get_status(pkt);
	case USB_REQUEST_SET_DESCRIPTOR:
		break;
	}
	return {};
}

}


namespace iface_request {

cranc::coro::Task<void> get_interface(control_data const& pkt) {
	if (not cur_active_config) {
		return {};
	}
	Interface* tgt_iface = interface_by_index(pkt.setup_pkt.wIndex);
	if (not tgt_iface) {
		return {};
	}

	if (not tgt_iface->cur_active_altsetting) {
		return {}; // something is broken
	}
	
	ep_in_config.data_buffer0[0] = *tgt_iface->cur_active_altsetting;
	ep0_tx_done.clear();
	flush_prefilled_buffer(ep_in_config, 1);
	return [=]() -> cranc::coro::Task<void> {
		co_await ep0_tx_done;
	}();
}

cranc::coro::Task<void> set_interface(control_data const& pkt) {
	if (not cur_active_config) {
		return {};
	}
	auto idx = pkt.setup_pkt.wIndex;
	Interface* tgt_iface{};
	for (auto& iface : cur_active_config->ifaces) {
		if (idx == 0) {
			tgt_iface = iface;
			break;
		}
		--idx;
	}
	if (not tgt_iface) {
		return {};
	}

	auto success = enable_altsetting(*tgt_iface, pkt.setup_pkt.wValue);

	if (not success) {
		return {};
	}
	
	ep0_tx_done.clear();
	flush_prefilled_buffer(ep_in_config, 0);
	return [=]() -> cranc::coro::Task<void> {
		co_await ep0_tx_done;
	}();
}

cranc::coro::Task<void> get_status(control_data const& pkt) {
	ep_in_config.data_buffer0[0] = 0;
	ep_in_config.data_buffer0[1] = 0;
	ep0_tx_done.clear();
	flush_prefilled_buffer(ep_in_config, 2);
	co_await ep0_tx_done;
}

cranc::coro::Task<void> handle_request(control_data const& pkt) {
	std::uint8_t req_type = pkt.setup_pkt.bmRequestType & USB_REQ_TYPE_TYPE_MASK;
	if (req_type != USB_REQ_TYPE_STANDARD) {
		auto* iface = interface_by_index(pkt.setup_pkt.wIndex);
		if (not iface) {
			return {};
		}
		auto response = iface->ccb(pkt);
		if (not response) {
			return {};
		}
		return [](auto resp) -> cranc::coro::Task<void> {
			auto c = chunkify<64>(resp);
			while (c.advance()) {
				auto chunk = c.get();
				tx_data(ep_in_config, chunk);
				co_await ep0_tx_done;
			}
		}(trim_span(*response, pkt.setup_pkt.wLength));
	}

	switch (pkt.setup_pkt.bRequest) {
	case USB_REQUEST_CLEAR_FEATURE:
	case USB_REQUEST_SET_FEATURE:
		break;
	case USB_REQUEST_GET_INTERFACE:
		return get_interface(pkt);
	case USB_REQUEST_SET_INTERFACE:
		return set_interface(pkt);
	case USB_REQUEST_GET_STATUS:
		return get_status(pkt);
		break;
	}
	return {};
}

}

namespace ep_request {

cranc::coro::Task<void> handle_request(control_data const& pkt) {
	auto& ep_cfg = config_for_ep(pkt.setup_pkt.wIndex);
	std::uint8_t req_type = pkt.setup_pkt.bmRequestType & USB_REQ_TYPE_TYPE_MASK;
	if (req_type != USB_REQ_TYPE_STANDARD) {
		if (not ep_cfg.descriptor) {
			return {};
		}
		auto response = ep_cfg.descriptor->ccb(pkt);
		if (not response) {
			return {};
		}
		return [](auto resp) -> cranc::coro::Task<void> {
			auto c = chunkify<64>(resp);
			while (c.advance()) {
				auto chunk = c.get();
				tx_data(ep_in_config, chunk);
				co_await ep0_tx_done;
			}
		}(trim_span(*response, pkt.setup_pkt.wLength));
	}


	auto ack = [](std::size_t i) -> cranc::coro::Task<void> {
		ep0_tx_done.clear();
		flush_prefilled_buffer(ep_in_config, i);
		co_await ep0_tx_done;
	};
	switch (pkt.setup_pkt.bRequest) {
	case USB_REQUEST_CLEAR_FEATURE:
		if (pkt.setup_pkt.wValue == USB_FEAT_ENDPOINT_HALT) {
			stall_ep(ep_cfg, false);
			return ack(0);
		}
	case USB_REQUEST_SET_FEATURE:
		if (pkt.setup_pkt.wValue == USB_FEAT_ENDPOINT_HALT) {
			stall_ep(ep_cfg, true);
			return ack(0);
		}
	case USB_REQUEST_GET_STATUS:
		ep_in_config.data_buffer0[0] = stall_ep_get(ep_cfg);
		return ack(1);
	case USB_REQUEST_SYNC_FRAME:
		break;
	}
	return {};
}

}

cranc::coro::Task<void> dispatch_control_packet(control_data pkt) {
	std::uint8_t recipient = pkt.setup_pkt.bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;
	switch (recipient) {
	case USB_REQ_TYPE_RECIPIENT_DEVICE:
		return dev_request::handle_request(pkt);
		break;
	case USB_REQ_TYPE_RECIPIENT_INTERFACE:
		return iface_request::handle_request(pkt);
		break;
	case USB_REQ_TYPE_RECIPIENT_ENDPOINT:
		return ep_request::handle_request(pkt);
		break;
	}
	return {};
}

cranc::coro::Task<void> handle_out_control_packet(control_data pkt) {
	std::array<std::uint8_t, 256> buffer;
	std::size_t read = 0;
	std::size_t to_read = (pkt.setup_pkt.bmRequestType & USB_DIR_IN) == 0 ? pkt.setup_pkt.wLength : 0;
	while (read < to_read) {
		ep0_rx_done.clear();
		start_rx(ep_out_config);
		auto received = co_await ep0_rx_done;
		std::memcpy(buffer.data() + read, received.data(), received.size());
		read += received.size();
	}
	pkt.data = std::span(buffer.data(), read);
	// step_trap();
	auto subtask = dispatch_control_packet(pkt);
	if (not subtask.handle) {
		// stall ep_in
		usb_hw_set->ep_stall_arm = USB_EP_STALL_ARM_EP0_IN_BITS;
		auto ctl = (*ep_in_config.buffer_control);
		ctl |= USB_BUF_CTRL_STALL;
		*ep_in_config.buffer_control = ctl;
		co_return;
	}
	co_await subtask;
}

cranc::coro::Task<void> control_worker() {
	cranc::coro::Task<void> subtask;
	while (true) {
		co_await on_setup_packet;
		subtask.terminate();

		// Reset PID to 1 for EP0 IN
		ep_in_config.next_pid = 1u;
		ep_out_config.next_pid = 1u;

		auto handle_stall = Finally{[&]{
			if (not subtask.handle) {
				// stall ep_in
				usb_hw_set->ep_stall_arm = USB_EP_STALL_ARM_EP0_IN_BITS;
				auto ctl = (*ep_in_config.buffer_control);
				ctl |= USB_BUF_CTRL_STALL;
				*ep_in_config.buffer_control = ctl;
			}
		}};


		control_data pkt{};
		std::memcpy(&pkt.setup_pkt, (void*)(&usb_dpram->setup_packet), sizeof(pkt));

		auto req_dir_in = (pkt.setup_pkt.bmRequestType & USB_DIR_IN) == USB_DIR_IN;

		if (req_dir_in) {
			auto dispatch_task = dispatch_control_packet(pkt);
			if (not dispatch_task.handle) {
				continue;
			}
			subtask = [](auto task) -> cranc::coro::Task<void> {
				co_await task;
				// await acknowledgement
				ep0_rx_done.clear();
				start_rx(ep_out_config);
				co_await ep0_rx_done;
			}(std::move(dispatch_task));
			continue;
		}

		if (not req_dir_in) {
			if (pkt.setup_pkt.wLength > 256) {
				continue;
			}
			subtask = handle_out_control_packet(pkt);
		}
	}
}


void usb_handle_buff_status() {
    uint32_t done_eps = usb_hw->buf_status;

	auto stat = usb_hw->sie_status;
	if (stat & (USB_SIE_STATUS_DATA_SEQ_ERROR_BITS 
				| USB_SIE_STATUS_RX_OVERFLOW_BITS 
				| USB_SIE_STATUS_BIT_STUFF_ERROR_BITS
				| USB_SIE_STATUS_CRC_ERROR_BITS)) {
	}

    for (uint i = 0; i < in_ep_configurations.size(); i++) {
		std::uint32_t in_bit  = 1 << (2*i + 0);
		std::uint32_t out_bit = 1 << (2*i + 1);
        if (done_eps & in_bit) {
            // clear this in advance
            usb_hw_clear->buf_status = in_bit;

			auto& ep_cfg = in_ep_configurations[i];
			if (not ep_cfg.descriptor) {
				continue;
			}
			if ((usb_hw->buf_cpu_should_handle & in_bit) == 0) {
				ep_cfg.descriptor->cb(ep_cfg.data_buffer0);
			} else {
				ep_cfg.descriptor->cb(ep_cfg.data_buffer1);
			}
        }
        if (done_eps & out_bit) {
            // clear this in advance
            usb_hw_clear->buf_status = out_bit;

			auto& ep_cfg = out_ep_configurations[i];
			uint16_t len = (*ep_cfg.buffer_control) & USB_BUF_CTRL_LEN_MASK;

			if (not ep_cfg.descriptor) {
				continue;
			}
			if ((usb_hw->buf_cpu_should_handle & out_bit) == 0) {
				ep_cfg.descriptor->cb(ep_cfg.data_buffer0.subspan(0, len));
			} else {
				ep_cfg.descriptor->cb(ep_cfg.data_buffer1.subspan(0, len));
			}
        }
    }
}

void usb_isr() {
	cranc::ISRTime isrTimer;
    // USB interrupt handler
    uint32_t status = usb_hw->ints;
    uint32_t handled = 0;

    // Setup packet
    if (status & USB_INTS_SETUP_REQ_BITS) {
        handled |= USB_INTS_SETUP_REQ_BITS;
        usb_hw_clear->sie_status = USB_SIE_STATUS_SETUP_REC_BITS;
		on_setup_packet();
    }

    // Buffer status, one or more buffers have completed
    if (status & USB_INTS_BUFF_STATUS_BITS) {
        handled |= USB_INTS_BUFF_STATUS_BITS;
        usb_handle_buff_status();
    }

    // Bus reset
    if (status & USB_INTS_BUS_RESET_BITS) {
        handled |= USB_INTS_BUS_RESET_BITS;
        usb_hw_clear->sie_status = USB_SIE_STATUS_BUS_RESET_BITS;
		usb_hw->dev_addr_ctrl = 0;
		if ( usb_hw->sie_ctrl & USB_SIE_CTRL_PULLUP_EN_BITS ) {
			rp2040_usb_device_enumeration_fix();
		}
    }

    if (status & USB_INTS_DEV_SOF_BITS) {
        handled |= USB_INTS_DEV_SOF_BITS;
		int frame_no = usb_hw->sof_rd & USB_SOF_RD_BITS;
		for (auto const& ep : out_ep_configurations) {
			if (not ep.descriptor) {
				continue;
			}
			ep.descriptor->sof_cb(frame_no);
		}
		for (auto const& ep : in_ep_configurations) {
			if (not ep.descriptor) {
				continue;
			}
			ep.descriptor->sof_cb(frame_no);
		}
	}

    if (status ^ handled) {
        panic("Unhandled IRQ 0x%x\n", (uint) (status ^ handled));
    }
}

struct : cranc::Module
{
	using cranc::Module::Module;
	void init() override {
		pico_unique_board_id_t board_id;
		pico_get_unique_board_id(&board_id);

		for (auto i=0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; ++i) {
				std::uint8_t lo =  (board_id.id[i] >> 0) & 0x0f;
				std::uint8_t hi =  (board_id.id[i] >> 4) & 0x0f;
				char lo_hex = lo < 10 ? lo + '0' : (lo - 10) + 'a';
				char hi_hex = hi < 10 ? hi + '0' : (hi - 10) + 'a';
				serial_str.descriptor.data[2*i + 0] = hi_hex;
				serial_str.descriptor.data[2*i + 1] = lo_hex;
		}
		serial_str.descriptor.data[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2] = 0;
		serial_str.descriptor.bLength = PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 4 + 2;

		out_ep_configurations[0] = {
			.descriptor = &ep0_out_desc,
			.endpoint_control = NULL,
			.buffer_control = &usb_dpram->ep_buf_ctrl[0].out,
			.data_buffer0 = usb_dpram->ep0_buf_a,
		};
		in_ep_configurations[0] = {
			.descriptor = &ep0_in_desc,
			.endpoint_control = NULL,
			.buffer_control = &usb_dpram->ep_buf_ctrl[0].in,
			.data_buffer0 = usb_dpram->ep0_buf_a,
		};
		for (auto i=1; i < out_ep_configurations.size(); ++i) {
			auto& config = out_ep_configurations[i];
			config.endpoint_control = &usb_dpram->ep_ctrl[i-1].out;
			config.buffer_control = &usb_dpram->ep_buf_ctrl[i].out;
		}
		for (auto i=1; i < in_ep_configurations.size(); ++i) {
			auto& config = in_ep_configurations[i];
			config.endpoint_control = &usb_dpram->ep_ctrl[i-1].in;
			config.buffer_control = &usb_dpram->ep_buf_ctrl[i].in;
		}

		control_task = control_worker();

		// Reset usb controller
		reset_block(RESETS_RESET_USBCTRL_BITS);
		unreset_block_wait(RESETS_RESET_USBCTRL_BITS);

		// Clear any previous state in dpram just in case
  		// memset(usb_hw, 0, sizeof(*usb_hw));
  		memset(usb_dpram, 0, sizeof(*usb_dpram));

		cranc::sleep(100ms);

				// Enable USB interrupt at processor
		irq_set_exclusive_handler(USBCTRL_IRQ, usb_isr);
		irq_set_enabled(USBCTRL_IRQ, true);

		// Mux the controller to the onboard usb phy
		usb_hw->muxing = USB_USB_MUXING_TO_PHY_BITS | USB_USB_MUXING_SOFTCON_BITS;

		// Force VBUS detect so the device thinks it is plugged into a host
		usb_hw->pwr = USB_USB_PWR_VBUS_DETECT_BITS | USB_USB_PWR_VBUS_DETECT_OVERRIDE_EN_BITS;

		// Enable the USB controller in device mode.
		usb_hw->main_ctrl = USB_MAIN_CTRL_CONTROLLER_EN_BITS;

		// Enable an interrupt per EP0 transaction
		usb_hw->sie_ctrl = USB_SIE_CTRL_EP0_INT_1BUF_BITS;

		// Enable interrupts for when a buffer is done, when the bus is reset,
		// and when a setup packet is received
		usb_hw->inte = USB_INTS_BUFF_STATUS_BITS |
					USB_INTS_BUS_RESET_BITS |
					USB_INTS_DEV_SOF_BITS |
					USB_INTS_SETUP_REQ_BITS;

		// Present full speed device by enabling pull up on DP
		usb_hw_set->sie_ctrl = USB_SIE_CTRL_PULLUP_EN_BITS;
	}
} _{1000};

}


void endpoint::set_stall(bool stalled) {
	auto& config = config_for_ep(descriptor.bEndpointAddress);
	return stall_ep(config, stalled);
}

bool endpoint::stalled() {
	auto& config = config_for_ep(descriptor.bEndpointAddress);
	return stall_ep_get(config);
}

void endpoint::start_rx() {
	auto& config = config_for_ep(descriptor.bEndpointAddress);
	usb::start_rx(config);
}

void endpoint::stop_rx() {
	auto& config = config_for_ep(descriptor.bEndpointAddress);
	usb::stop_rx(config);
}


std::span<std::uint8_t> endpoint::getNextTxBuffer() {
	auto& config = config_for_ep(descriptor.bEndpointAddress);
	return get_buffer(config);
}

std::size_t endpoint::send(std::span<const std::uint8_t> buffer) {
	auto& config = config_for_ep(descriptor.bEndpointAddress);
	return tx_data(config, buffer);
}

}
