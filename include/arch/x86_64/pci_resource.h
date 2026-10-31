#ifndef ARCH_X86_64_PCI_RESOURCE_H
#define ARCH_X86_64_PCI_RESOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/pci.h"

#define PCI_MMIO_LEASE_SLOTS 32U

struct pci_mmio_range {
	uint64_t physical_page_base;
	size_t page_count;
	size_t resource_offset;
};

struct pci_mmio_lease {
	uint64_t generation;
	uint64_t address;
	size_t length;
	uint16_t slot;
};

/* BAR size is accepted only when its source has supplied a validated,
 * nonzero aperture. Current enumeration intentionally leaves it unknown. */
bool pci_bar_resource_valid(const struct pci_bar_resource *resource);
bool pci_mmio_range_calculate(const struct pci_bar_resource *resource,
	uint64_t offset, size_t length, struct pci_mmio_range *range);
bool pci_mmio_map(const struct pci_bar_resource *resource, uint64_t offset,
	size_t length, struct pci_mmio_lease *lease);
bool pci_mmio_lease_address(const struct pci_mmio_lease *lease,
	volatile void **address);
bool pci_mmio_release(struct pci_mmio_lease *lease);

#endif
