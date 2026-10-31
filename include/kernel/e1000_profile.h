#ifndef KERNEL_E1000_PROFILE_H
#define KERNEL_E1000_PROFILE_H

#include <stdbool.h>

#include "arch/x86_64/pci.h"

#define E1000_QEMU_VENDOR_ID 0x8086U
#define E1000_QEMU_DEVICE_ID 0x100eU
#define E1000_QEMU_BAR0_APERTURE 0x20000ULL
#define E1000_QEMU_BAR1_IO_APERTURE 0x40ULL

struct e1000_qemu_resources {
	struct pci_bar_resource register_mmio;
	struct pci_bar_resource register_io;
};

/* Validates the explicit QEMU 82540EM profile only. It does not size a BAR
 * generically or authorize mapping until pci_mmio_map validates the range. */
bool e1000_qemu_profile_validate(const struct pci_device_info *device,
	struct e1000_qemu_resources *resources);

#endif
