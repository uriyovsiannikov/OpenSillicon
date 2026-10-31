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

typedef bool (*pci_config_reader)(void *context, uint16_t segment,
	uint8_t bus, uint8_t device, uint8_t function, uint16_t offset,
	uint32_t *value);
typedef bool (*pci_device_callback)(const struct pci_device_info *device,
	void *context);

bool pci_config_read(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, unsigned int width, uint32_t *value);
bool pci_config_write(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, unsigned int width, uint32_t value);
bool pci_enumerate(pci_device_callback callback, void *context);
bool pci_enumerate_with_reader(pci_config_reader reader, void *reader_context,
	const struct acpi_mcfg_region *regions, size_t region_count,
	pci_device_callback callback, void *callback_context);

#endif
