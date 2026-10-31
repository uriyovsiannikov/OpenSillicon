#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kernel/e1000_profile.h"

static struct pci_device_info
valid_device(void)
{
	return (struct pci_device_info){
		.class_code = 0x02,
		.subclass = 0,
		.programming_interface = 0,
		.header_type = 0,
		.vendor_id = E1000_QEMU_VENDOR_ID,
		.device_id = E1000_QEMU_DEVICE_ID,
		.bars = { 0x80040000U, 0x00006041U, 0, 0, 0, 0 }
	};
}

int
main(void)
{
	struct pci_device_info device = valid_device();
	struct e1000_qemu_resources resources;

	assert(e1000_qemu_profile_validate(&device, &resources));
	assert(resources.register_mmio.type == PCI_BAR_MEMORY32);
	assert(resources.register_mmio.base == 0x80040000ULL);
	assert(resources.register_mmio.aperture_size == 0x20000ULL);
	assert(resources.register_io.type == PCI_BAR_IO);
	assert(resources.register_io.base == 0x6040ULL);
	assert(resources.register_io.aperture_size == 0x40ULL);
	assert(!e1000_qemu_profile_validate(NULL, &resources));
	assert(!e1000_qemu_profile_validate(&device, NULL));

	device.device_id = 0x10d3; /* e1000e/82574L, not this profile */
	assert(!e1000_qemu_profile_validate(&device, &resources));
	device = valid_device();
	device.bars[0] = 0x80040004U; /* 64-bit memory BAR is not this profile */
	assert(!e1000_qemu_profile_validate(&device, &resources));
	device = valid_device();
	device.bars[0] = 0x80050000U; /* wrong 128 KiB alignment */
	assert(!e1000_qemu_profile_validate(&device, &resources));
	device = valid_device();
	device.bars[0] |= 8U; /* prefetchable unexpected BAR */
	assert(!e1000_qemu_profile_validate(&device, &resources));
	device = valid_device();
	device.bars[1] = 0x00006021U; /* unexpected I/O aperture alignment */
	assert(!e1000_qemu_profile_validate(&device, &resources));
	device = valid_device();
	device.bars[2] = 0x80080000U; /* unexpected extra BAR */
	assert(!e1000_qemu_profile_validate(&device, &resources));
	device = valid_device();
	device.header_type = 1;
	assert(!e1000_qemu_profile_validate(&device, &resources));
	puts("QEMU e1000 device and BAR profile validation tests passed");
	return 0;
}
