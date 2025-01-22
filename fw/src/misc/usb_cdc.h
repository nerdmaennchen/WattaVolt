#pragma once

#include "cranc/util/function.h"

#include "usb/usb_dev.h"

#include <string_view>


struct USB_CDC_Device {
    using rx_cb      = cranc::function<void(std::span<std::uint8_t const>)>;
    using tx_done_cb = cranc::function<void()>;
    USB_CDC_Device(usb::USB_String& name, std::uint8_t data_ep_out, std::uint8_t data_ep_in, std::uint8_t notif_ep, rx_cb on_rx, tx_done_cb on_tx_done);

    std::size_t send(std::span<std::uint8_t const> out) {
        if (not active) {
            on_tx_done();
            return out.size();
        }
        return transfer_eps[1].send(out);
    }
    std::size_t send(std::span<char const> out) {
        if (not active) {
            on_tx_done();
            return out.size();
        }
        return transfer_eps[1].send({reinterpret_cast<std::uint8_t const*>(&(out[0])), out.size()});
    }
    std::size_t send(std::string_view out) {
        if (not active) {
            on_tx_done();
            return out.size();
        }
        return transfer_eps[1].send({reinterpret_cast<std::uint8_t const*>(&(out[0])), out.size()});
    }

private:
    struct __attribute__((packed)) {
        std::uint32_t bit_rate;
        std::uint8_t  stop_bits; ///< 0: 1 stop bit - 1: 1.5 stop bits - 2: 2 stop bits
        std::uint8_t  parity;    ///< 0: None - 1: Odd - 2: Even - 3: Mark - 4: Space
        std::uint8_t  data_bits; ///< can be 5, 6, 7, 8 or 16
    } cdc_line_coding;

    rx_cb on_rx;
    tx_done_cb on_tx_done;
    bool active{false};

    std::array<usb::endpoint, 2> transfer_eps  = {
        usb::endpoint{ // out ep
            .descriptor = {
                .bmAttributes = USB_TRANSFER_TYPE_BULK,
                .wMaxPacketSize = 64,
                .bInterval = 0,
            },
            .double_buffered = false,
            .cb = [this](std::span<std::uint8_t> data) {
                on_rx(data);
                transfer_eps[0].start_rx();
            }
        },
        usb::endpoint{ // in ep
            .descriptor = {
                .bmAttributes = USB_TRANSFER_TYPE_BULK,
                .wMaxPacketSize = 64,
                .bInterval = 0,
            },
            .double_buffered = false,
            .cb = [this](std::span<std::uint8_t>) {
                on_tx_done();
            }
        },
    };

    std::array<usb::endpoint, 1> notif_eps  = {
        usb::endpoint{
            .descriptor = {
                .bmAttributes = USB_TRANSFER_TYPE_INTERRUPT,
                .wMaxPacketSize = 8,
                .bInterval = 16,
            },
            .double_buffered = false,
            .cb = [](std::span<std::uint8_t> data) {},
        },
    };

    std::array<usb::usb_iface_setting, 1> control_iface_settings {
        usb::usb_iface_setting{
            .descriptor = {
                .bInterfaceClass = 0x02, // Communications
                .bInterfaceSubClass = 0x02, // Abstract (modem)
                .bInterfaceProtocol = 0x00,
            },
            .endpoints = notif_eps,
        },
    };
    std::array<usb::usb_iface_setting, 1> data_iface_settings {
        usb::usb_iface_setting{
            .descriptor = {
                .bInterfaceClass    = 0x0a, // CDC Data
                .bInterfaceSubClass = 0x00, // whatever
                .bInterfaceProtocol = 0x00,
            },
            .endpoints = transfer_eps,
        },
    };

    std::array<usb::Interface, 2> interfaces {
        usb::Interface{ default_usb_dev::config, control_iface_settings},
        usb::Interface{ default_usb_dev::config, data_iface_settings},
    };
    usb::InterfaceAssociation iface_association { default_usb_dev::config, 2, 2, 0, interfaces};


    struct __attribute__((packed)) CDC_Extra {
        struct __attribute__((packed)) {
            std::uint8_t  bLength             = sizeof(header);
            std::uint8_t  bDescriptortype     = 0x24; // CS INTERFACE
            std::uint8_t  bDescriptorsubtype  = 0x00; // Header
            std::uint16_t bcdCDC              = 0x0120;
        } header;

        struct __attribute__((packed)) {
            std::uint8_t bLength = sizeof(acm_functional_decriptor);
            std::uint8_t bDescriptortype     = 0x24; // CS INTERFACE
            std::uint8_t bDescriptorsubtype  = 0x02; // ABSTRACT CONTROL MANAGEMENT
            std::uint8_t bmCapabilities      = 0x00; // Supported subset of ACM commands: line coding
        } acm_functional_decriptor;

        struct __attribute__((packed)) {
            std::uint8_t bLength = sizeof(union_functional_descriptor);
            std::uint8_t bDescriptortype        = 0x24; // CS INTERFACE
            std::uint8_t bDescriptorsubtype     = 0x06; // UNION
            std::uint8_t bControlInterface      = 0;
            std::uint8_t bSubordinateInterface0 = 0;
        } union_functional_descriptor;

        struct __attribute__((packed)) {
            std::uint8_t bLength = sizeof(union_functional_descriptor);
            std::uint8_t bDescriptortype        = 0x24; // CS INTERFACE
            std::uint8_t bDescriptorsubtype     = 0x01; //  CALL MANAGEMENT
            std::uint8_t bmCapabilities         = 0x00;
            std::uint8_t bDataInterface         = 0;
        } call_management_functional_descriptor;
    } cdc_extra;
};