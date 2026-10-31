#ifndef ARCH_X86_64_PCI_H
#define ARCH_X86_64_PCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/acpi.h"

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

enum pci_bar_type {
	PCI_BAR_UNUSED,
	PCI_BAR_IO,
	PCI_BAR_MEMORY32,
	PCI_BAR_MEMORY64
};

/*
 * Decodes the currently assigned BAR address and type. PCI BAR sizing is
 * deliberately separate: aperture_size is zero until a safe sizing policy
 * is implemented, so this result is not by itself permission to map a BAR.
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
bool pci_bar_decode(const struct pci_device_info *device, unsigned int index,
	struct pci_bar_resource *resource);
bool pci_enumerate(pci_device_callback callback, void *context);
bool pci_enumerate_with_reader(pci_config_reader reader, void *reader_context,
	const struct acpi_mcfg_region *regions, size_t region_count,
	pci_device_callback callback, void *callback_context);

#endif
