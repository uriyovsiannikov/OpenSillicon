#include <stdint.h>

#include "kernel/e1000_profile.h"

bool
e1000_qemu_profile_validate(const struct pci_device_info *device,
	struct e1000_qemu_resources *resources)
{
	uint32_t bar0;
	uint32_t bar1;
	uint64_t mmio_base;
	uint64_t io_base;

	if (device == NULL || resources == NULL ||
	    device->vendor_id != E1000_QEMU_VENDOR_ID ||
	    device->device_id != E1000_QEMU_DEVICE_ID ||
	    (device->header_type & 0x7fU) != 0 || device->class_code != 0x02U ||
	    device->subclass != 0x00U || device->programming_interface != 0x00U)
		return false;
	bar0 = device->bars[0];
	bar1 = device->bars[1];
	if (bar0 == 0 || bar0 == UINT32_MAX || (bar0 & 1U) != 0 ||
	    ((bar0 >> 1) & 3U) != 0 || (bar0 & 8U) != 0 ||
	    bar1 == 0 || bar1 == UINT32_MAX || (bar1 & 1U) == 0)
		return false;
	mmio_base = bar0 & ~0xfU;
	io_base = bar1 & ~0x3U;
	if (mmio_base == 0 ||
	    (mmio_base & (E1000_QEMU_BAR0_APERTURE - 1)) != 0 ||
	    mmio_base > UINT32_MAX - (E1000_QEMU_BAR0_APERTURE - 1) ||
	    io_base == 0 || (io_base & (E1000_QEMU_BAR1_IO_APERTURE - 1)) != 0)
		return false;
	for (size_t i = 2; i < 6; i++)
		if (device->bars[i] != 0)
			return false;
	*resources = (struct e1000_qemu_resources){
		.register_mmio = {
			.type = PCI_BAR_MEMORY32,
			.base = mmio_base,
			.aperture_size = E1000_QEMU_BAR0_APERTURE,
			.index = 0,
			.slots = 1,
			.prefetchable = false
		},
		.register_io = {
			.type = PCI_BAR_IO,
			.base = io_base,
			.aperture_size = E1000_QEMU_BAR1_IO_APERTURE,
			.index = 1,
			.slots = 1,
			.prefetchable = false
		}
	};
	return true;
}
