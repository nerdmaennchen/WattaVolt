#pragma once

#include "usb_common.h"

#include "cranc/util/function.h"
#include "cranc/util/Singleton.h"
#include "cranc/util/LinkedList.h"

#include <span>
#include <cstdint>
#include <string_view>
#include <optional>

namespace usb {

struct Interface;
struct InterfaceAssociation;

struct USB_String : cranc::util::GloballyLinkedList<USB_String> {
    struct __attribute__((packed)) {
        std::uint8_t bLength;
        std::uint8_t bDescriptorType;
        std::array<std::uint16_t, 126> data;
    } descriptor;

    USB_String(std::string_view sv) {
        descriptor.bLength = 2 + sv.size() * 2;
        descriptor.bDescriptorType = USB_DT_STRING;
        for (auto i=0U; i < sv.size(); ++i) {
            descriptor.data[i] = sv[i];
        }
    }

    std::uint8_t index() const {
        std::uint8_t idx = 1;
        auto& list = cranc::util::GloballyLinkedList<USB_String>::getHead();
        for (auto const& s : list) {
            if (&s == this) {
                break;
            }
            ++idx;
        }
        return idx;
    }

    operator std::uint8_t() const {
        return index();
    }

    std::span<const std::uint8_t> as_descriptor() const {
        return {reinterpret_cast<std::uint8_t const*>(&descriptor), descriptor.bLength};
    }
};

struct Configuration : cranc::util::GloballyLinkedList<Configuration> {
	Configuration(	uint8_t bmAttributes,
					uint8_t bMaxPower,
					USB_String const* iConfig=nullptr)
		: cranc::util::GloballyLinkedList<Configuration>{}
		, _bmAttributes{bmAttributes}
		, _bMaxPower{bMaxPower}
		, _iConfig{iConfig}
	{}

    std::uint8_t index() const {
        std::uint8_t idx = 1;
        auto& list = cranc::util::GloballyLinkedList<Configuration>::getHead();
        for (auto const& s : list) {
            if (&s == this) {
                break;
            }
            ++idx;
        }
        return idx;
    }

    uint8_t _bmAttributes;
    uint8_t _bMaxPower;
	USB_String const* _iConfig;

	cranc::util::LinkedList<Interface> ifaces{};
	cranc::util::LinkedList<InterfaceAssociation> iface_associations{};

    cranc::function<void(bool)> on_set_active;
};

using opt_response = std::optional<std::span<const std::uint8_t>>;
inline opt_response ack_response = std::span<const std::uint8_t>{};

struct control_data {
    usb_setup_packet setup_pkt;
    std::span<const std::uint8_t> data; // optional data from host
};
using control_cb = cranc::function<opt_response(control_data const&)>;

struct endpoint {
    usb_endpoint_descriptor descriptor;
    std::span<const std::uint8_t> extra{};

    bool double_buffered{};

	cranc::function<void(std::span<std::uint8_t>)> cb;
	cranc::function<void(int frame_no)> sof_cb;
	control_cb ccb;

    void set_stall(bool);
    void start_rx();
    void stop_rx();

    bool stalled();

    std::span<std::uint8_t> getNextTxBuffer();
    std::size_t send(std::span<const std::uint8_t> buffer);
};

struct usb_iface_setting {
    usb_interface_descriptor descriptor;
    std::span<const endpoint> endpoints;
    std::span<const std::uint8_t> extra{};
};

struct Interface : cranc::util::LinkedList<Interface> {
	Interface(Configuration& config, std::span<usb_iface_setting> _altsettings)
        : cranc::util::LinkedList<Interface>{}
        , config{config}
        , altsettings{_altsettings}
    {
        config.ifaces.insertBefore(*this);
    }

    std::uint8_t index() const {
        std::uint8_t idx = 0;
        for (auto const& s : config.ifaces) {
            if (&s == this) {
                break;
            }
            ++idx;
        }
        return idx;
    }
    Configuration& config;

    std::span<usb_iface_setting> altsettings;

    cranc::function<void()> on_altsetting_changed;
	control_cb ccb;

    std::optional<std::size_t> cur_active_altsetting {}; // will be set by the driver
};

struct InterfaceAssociation : cranc::util::LinkedList<InterfaceAssociation> {
	InterfaceAssociation(Configuration& config, std::uint8_t func_class, std::uint8_t func_sub_class, std::uint8_t func_prot, std::span<const Interface> interfaces, std::uint8_t iFunction=0)
        : cranc::util::LinkedList<InterfaceAssociation>{}
    {
        config.iface_associations.insertNext(*this);
        descriptor.bLength = sizeof(descriptor);
        descriptor.bDescriptorType   = USB_DT_IFACE_ASSOC;
        descriptor.bFirstInterface   = interfaces[0].index();
        descriptor.bInterfaceCount   = interfaces.size();
        descriptor.bFunctionClass    = func_class;
        descriptor.bFunctionSubClass = func_sub_class;
        descriptor.bFunctionProtocol = func_prot;
        descriptor.iFunction         = iFunction;
    }

    usb_interface_association_descriptor descriptor;
};


namespace literals {

inline USB_String operator"" _usb_str (const char* str, std::size_t s) {
    return USB_String{{str, s}};
}

}

}
