#include "usb_cdc.h"

#include "util/span_helpers.h"
#include <cstring>


USB_CDC_Device::USB_CDC_Device(usb::USB_String& name, std::uint8_t data_ep_out, std::uint8_t data_ep_in, std::uint8_t notif_ep, rx_cb on_rx, tx_done_cb on_tx_done)
    : on_rx{on_rx}
    , on_tx_done{on_tx_done}
{
    control_iface_settings[0].extra = to_span_c(cdc_extra);
    interfaces[0].ccb = [this](usb::control_data const& req) -> usb::opt_response {
        if (req.setup_pkt.bRequest == 0x20) { // set line coding
            if (sizeof(cdc_line_coding) == req.data.size()) {
                std::memcpy(&cdc_line_coding, req.data.data(), req.data.size());
                return usb::ack_response;
            }
        }
        if (req.setup_pkt.bRequest == 0x21) { // get line coding
            return to_span_c(cdc_line_coding);
        }
        if (req.setup_pkt.bRequest == 0x22) { // set control line state
            return usb::ack_response;
        }
        if (req.setup_pkt.bRequest == 0x23) { // send break
            return usb::ack_response;
        }
        return {};
    };

    interfaces[1].on_altsetting_changed = [this]() {
        active = false;
        if (not interfaces[0].cur_active_altsetting) {
            return;
        }
        active = true;
        transfer_eps[0].start_rx();
    };

    control_iface_settings[0].descriptor.iInterface = name;
    data_iface_settings[0].descriptor.iInterface = name;
    iface_association.descriptor.iFunction = name;
    
    cdc_extra.union_functional_descriptor.bControlInterface      = interfaces[0].index();
    cdc_extra.union_functional_descriptor.bSubordinateInterface0 = interfaces[1].index();

    cdc_extra.call_management_functional_descriptor.bDataInterface = interfaces[1].index();

    transfer_eps[0].descriptor.bEndpointAddress = data_ep_out;
    transfer_eps[1].descriptor.bEndpointAddress = data_ep_in;
    notif_eps[0].descriptor.bEndpointAddress = notif_ep;
}

