// SPDX-FileCopyrightText: 2026 Northern.tech AS
// SPDX-License-Identifier: Apache-2.0

#ifndef DUTLER_USB_DESCRIPTORS_H
#define DUTLER_USB_DESCRIPTORS_H

// Hardware serial (the flash chip's unique ID) as a NUL-terminated hex string —
// the same value used for the USB iSerial descriptor. Stable for the life of the
// board.
const char *usb_get_serial(void);

// Bounce the USB link (flush, disconnect, reconnect) so the host re-reads the
// descriptors — used after the device name (USB product string) changes. Any USB
// serial handle open at that instant is dropped and must be reopened.
void usb_reenumerate(void);

#endif  // DUTLER_USB_DESCRIPTORS_H
