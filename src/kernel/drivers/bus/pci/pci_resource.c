#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/pci_resource.h"
#include "kernel/log.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"

#define PCI_RESOURCE_PAGE_SIZE 4096ULL
#define PCI_MMIO_MAX_PAGES (VMM_DEVICE_WINDOW_SIZE / PCI_RESOURCE_PAGE_SIZE)

struct lease_record {
	uint64_t generation;
	uint64_t virtual_base;
	uint64_t physical_page_base;
	size_t page_count;
	size_t mapped_pages;
	size_t released_pages;
	struct pci_device_lease device;
	bool active;
};

static struct lease_record leases[PCI_MMIO_LEASE_SLOTS];
static struct spinlock lease_lock = SPINLOCK_INITIALIZER;

static bool
power_of_two_u64(uint64_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

bool
pci_bar_resource_valid(const struct pci_bar_resource *resource)
{
	uint64_t end;

	if (resource == NULL ||
	    (resource->type != PCI_BAR_MEMORY32 &&
	    resource->type != PCI_BAR_MEMORY64) ||
	    resource->base < 0x100000ULL ||
	    resource->aperture_size < 16 ||
	    !power_of_two_u64(resource->aperture_size) ||
	    (resource->base & (resource->aperture_size - 1)) != 0 ||
	    resource->base > UINT64_MAX - resource->aperture_size)
		return false;
	end = resource->base + resource->aperture_size;
	if (resource->type == PCI_BAR_MEMORY32 &&
	    (resource->base > UINT32_MAX || end > (1ULL << 32)))
		return false;
	return resource->slots == (resource->type == PCI_BAR_MEMORY64 ? 2 : 1);
}

bool
pci_mmio_range_calculate(const struct pci_bar_resource *resource,
	uint64_t offset, size_t length, struct pci_mmio_range *range)
{
	uint64_t start;
	uint64_t requested_end;
	uint64_t aperture_end;
	uint64_t page_start;
	uint64_t page_end;
	uint64_t aperture_page_start;
	uint64_t aperture_page_end;

	if (range == NULL || !pci_bar_resource_valid(resource) || length == 0 ||
	    offset > resource->aperture_size ||
	    (uint64_t)length > resource->aperture_size - offset ||
	    resource->base > UINT64_MAX - offset)
		return false;
	start = resource->base + offset;
	if (start > UINT64_MAX - length)
		return false;
	requested_end = start + length;
	aperture_end = resource->base + resource->aperture_size;
	page_start = start & ~(PCI_RESOURCE_PAGE_SIZE - 1);
	if (requested_end > UINT64_MAX - (PCI_RESOURCE_PAGE_SIZE - 1))
		return false;
	page_end = (requested_end + PCI_RESOURCE_PAGE_SIZE - 1) &
	    ~(PCI_RESOURCE_PAGE_SIZE - 1);
	aperture_page_start = resource->base & ~(PCI_RESOURCE_PAGE_SIZE - 1);
	if (aperture_end > UINT64_MAX - (PCI_RESOURCE_PAGE_SIZE - 1))
		return false;
	aperture_page_end = (aperture_end + PCI_RESOURCE_PAGE_SIZE - 1) &
	    ~(PCI_RESOURCE_PAGE_SIZE - 1);
	if (page_start < aperture_page_start || page_end > aperture_page_end ||
	    page_end <= page_start ||
	    (page_end - page_start) / PCI_RESOURCE_PAGE_SIZE > PCI_MMIO_MAX_PAGES)
		return false;
	range->physical_page_base = page_start;
	range->page_count = (size_t)((page_end - page_start) /
	    PCI_RESOURCE_PAGE_SIZE);
	range->resource_offset = (size_t)(start - page_start);
	return true;
}

static bool
lease_overlaps(uint64_t base, size_t pages,
	const struct lease_record *lease)
{
	uint64_t bytes = (uint64_t)pages * PCI_RESOURCE_PAGE_SIZE;
	uint64_t other_bytes = (uint64_t)lease->page_count * PCI_RESOURCE_PAGE_SIZE;

	return base < lease->virtual_base + other_bytes &&
	    lease->virtual_base < base + bytes;
}

static bool
find_virtual_range(size_t page_count, uint64_t *base_out)
{
	uint64_t start = vmm_vmalloc_device_start();
	uint64_t end = vmm_vmalloc_device_end();
	uint64_t bytes = (uint64_t)page_count * PCI_RESOURCE_PAGE_SIZE;
	uint64_t candidate;

	if (base_out == NULL || page_count == 0 || start >= end ||
	    bytes > end - start)
		return false;
	candidate = start;
	while (candidate <= end - bytes) {
		bool overlap = false;
		uint64_t next = candidate;

		for (size_t i = 0; i < PCI_MMIO_LEASE_SLOTS; i++) {
			if (leases[i].active && lease_overlaps(candidate, page_count,
			    &leases[i])) {
				uint64_t lease_end = leases[i].virtual_base +
				    (uint64_t)leases[i].page_count * PCI_RESOURCE_PAGE_SIZE;
				if (lease_end > next)
					next = lease_end;
				overlap = true;
			}
		}
		if (!overlap) {
			*base_out = candidate;
			return true;
		}
		candidate = next;
	}
	return false;
}

static bool
pci_mmio_map_with_flags(const struct pci_device_lease *device,
	const struct pci_bar_resource *resource, uint64_t offset, size_t length,
	struct pci_mmio_lease *lease, unsigned int map_flags)
{
	struct pci_mmio_range range;
	struct lease_record *record = NULL;
	uint64_t virtual_base;
	uint64_t flags;
	size_t mapped;

	if (lease == NULL || !pci_mmio_range_calculate(resource, offset, length,
	    &range) || !pci_device_mmio_acquire(device, resource))
		return false;
	if (!vmm_physical_range_mmio_safe(range.physical_page_base,
	    range.page_count * (size_t)PCI_RESOURCE_PAGE_SIZE)) {
		pci_device_mmio_drop(device);
		return false;
	}
	flags = spinlock_lock_irqsave(&lease_lock);
	if (!find_virtual_range(range.page_count, &virtual_base)) {
		spinlock_unlock_irqrestore(&lease_lock, flags);
		pci_device_mmio_drop(device);
		return false;
	}
	for (size_t i = 0; i < PCI_MMIO_LEASE_SLOTS; i++) {
		if (!leases[i].active) {
			record = &leases[i];
			break;
		}
	}
	if (record == NULL) {
		spinlock_unlock_irqrestore(&lease_lock, flags);
		pci_device_mmio_drop(device);
		return false;
	}
	for (mapped = 0; mapped < range.page_count; mapped++) {
		if (!vmm_map_page(vmm_kernel_address_space(),
		    virtual_base + mapped * PCI_RESOURCE_PAGE_SIZE,
		    range.physical_page_base + mapped * PCI_RESOURCE_PAGE_SIZE,
		    map_flags | VMM_UNCACHED))
			break;
	}
	if (mapped != range.page_count) {
		while (mapped != 0) {
			uint64_t ignored;
			mapped--;
			if (!vmm_unmap_page(vmm_kernel_address_space(),
			    virtual_base + mapped * PCI_RESOURCE_PAGE_SIZE, &ignored)) {
				klog("OpenSillicon panic: partial PCI MMIO map rollback failed\n");
				kernel_halt();
			}
		}
		spinlock_unlock_irqrestore(&lease_lock, flags);
		pci_device_mmio_drop(device);
		return false;
	}
	record->generation++;
	if (record->generation == 0)
		record->generation++;
	record->virtual_base = virtual_base;
	record->physical_page_base = range.physical_page_base;
	record->page_count = range.page_count;
	record->mapped_pages = range.page_count;
	record->released_pages = 0;
	record->device = *device;
	record->active = true;
	*lease = (struct pci_mmio_lease){
		.generation = record->generation,
		.address = virtual_base + range.resource_offset,
		.length = length,
		.slot = (uint16_t)(record - leases)
	};
	spinlock_unlock_irqrestore(&lease_lock, flags);
	return true;
}

bool
pci_mmio_map(const struct pci_device_lease *device,
	const struct pci_bar_resource *resource, uint64_t offset, size_t length,
	struct pci_mmio_lease *lease)
{
	return pci_mmio_map_with_flags(device, resource, offset, length, lease,
	    VMM_WRITE);
}

bool
pci_mmio_map_readonly(const struct pci_device_lease *device,
	const struct pci_bar_resource *resource, uint64_t offset, size_t length,
	struct pci_mmio_lease *lease)
{
	return pci_mmio_map_with_flags(device, resource, offset, length, lease, 0);
}

static struct lease_record *
lease_lookup(const struct pci_mmio_lease *lease)
{
	struct lease_record *record;

	if (lease == NULL || lease->slot >= PCI_MMIO_LEASE_SLOTS ||
	    lease->generation == 0)
		return NULL;
	record = &leases[lease->slot];
	return record->active && record->generation == lease->generation ?
	    record : NULL;
}

bool
pci_mmio_lease_address(const struct pci_mmio_lease *lease,
	volatile void **address)
{
	uint64_t flags = spinlock_lock_irqsave(&lease_lock);
	if (lease_lookup(lease) == NULL || address == NULL) {
		spinlock_unlock_irqrestore(&lease_lock, flags);
		return false;
	}
	*address = (volatile void *)(uintptr_t)lease->address;
	spinlock_unlock_irqrestore(&lease_lock, flags);
	return true;
}

bool
pci_mmio_release(struct pci_mmio_lease *lease)
{
	uint64_t flags = spinlock_lock_irqsave(&lease_lock);
	struct lease_record *record = lease_lookup(lease);

	if (record == NULL) {
		spinlock_unlock_irqrestore(&lease_lock, flags);
		return false;
	}
	while (record->released_pages < record->mapped_pages) {
		size_t page = record->released_pages;
		uint64_t unmapped;

		if (!vmm_unmap_page(vmm_kernel_address_space(),
		    record->virtual_base + page * PCI_RESOURCE_PAGE_SIZE, &unmapped) ||
		    unmapped != record->physical_page_base +
		    page * PCI_RESOURCE_PAGE_SIZE) {
			spinlock_unlock_irqrestore(&lease_lock, flags);
			return false;
		}
		record->released_pages++;
	}
	record->active = false;
	pci_device_mmio_drop(&record->device);
	record->device.generation = 0;
	record->device.slot = 0;
	lease->address = 0;
	lease->length = 0;
	lease->generation = 0;
	spinlock_unlock_irqrestore(&lease_lock, flags);
	return true;
}
