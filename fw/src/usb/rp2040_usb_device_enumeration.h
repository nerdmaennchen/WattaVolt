#pragma once

/*! \brief Perform a brute force workaround for USB device enumeration issue
 * \ingroup pico_fix
 *
 * This method should be called during the IRQ handler for a bus reset
 */
void rp2040_usb_device_enumeration_fix(void);