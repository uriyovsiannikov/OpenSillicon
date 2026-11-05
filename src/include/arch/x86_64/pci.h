#ifndef ARCH_X86_64_PCI_H
#define ARCH_X86_64_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/acpi.h"

#define PCI_DEVICE_LEASE_SLOTS 32U

struct pci_device_info {
	uint16_t segment;
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint8_t class_code;
	uint8_t subclass;
	uint8_t programming_interface;
	uint8_t header_type;
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t command;
	uint32_t bars[6];
};

struct pci_device_lease {
	uint64_t generation;
	uint16_t slot;
};

enum pci_bar_type {
	PCI_BAR_UNUSED,
	PCI_BAR_IO,
	PCI_BAR_MEMORY32,
	PCI_BAR_MEMORY64
};

/*
 * Decodes the currently assigned BAR address and type. PCI BAR sizing is
 * deliberately separate: aperture_size is zero until an owning driver's
 * claim has completed safe sizing, so decode alone does not permit mapping.
 */
struct pci_bar_resource {
	enum pci_bar_type type;
	uint64_t base;
	uint64_t aperture_size;
	uint8_t index;
	uint8_t slots;
	bool prefetchable;
};

typedef bool (*pci_config_reader)(void *context, uint16_t segment,
	uint8_t bus, uint8_t device, uint8_t function, uint16_t offset,
	uint32_t *value);
typedef bool (*pci_device_callback)(const struct pci_device_info *device,
	void *context);

bool pci_config_read(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, unsigned int width, uint32_t *value);
bool pci_config_write(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, unsigned int width, uint32_t value);
/* Claims one enumerated function until the caller has stopped using it. */
bool pci_device_claim(const struct pci_device_info *device,
	struct pci_device_lease *lease);
bool pci_device_release(struct pci_device_lease *lease);
/* Internal resource-manager hooks: pin an exactly probed BAR while mapped. */
bool pci_device_mmio_acquire(const struct pci_device_lease *lease,
	const struct pci_bar_resource *resource);
void pci_device_mmio_drop(const struct pci_device_lease *lease);
/* QEMU-only transitional VirtIO RNG profile helper. Sizes BAR0 while decode
 * and bus mastering are disabled, then assigns the requested I/O base if the
 * BAR is unassigned. This is not a general PCI resource allocator. */
bool pci_virtio_rng_legacy_io_assign(const struct pci_device_info *device,
	uint16_t requested_base, uint32_t *assigned_bar);
bool pci_bar_decode(const struct pci_device_info *device, unsigned int index,
	struct pci_bar_resource *resource);
/* Sizes an assigned memory BAR only for a claimed device. Decode, bus
 * mastering and the original BAR are restored and verified. */
bool pci_bar_probe_size(const struct pci_device_lease *lease,
	unsigned int index, struct pci_bar_resource *resource);
bool pci_enumerate(pci_device_callback callback, void *context);
bool pci_enumerate_with_reader(pci_config_reader reader, void *reader_context,
	const struct acpi_mcfg_region *regions, size_t region_count,
	pci_device_callback callback, void *callback_context);

#endif
