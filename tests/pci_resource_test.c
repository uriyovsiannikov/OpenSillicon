#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "arch/x86_64/pci_resource.h"
#include "kernel/vmm.h"

#define PAGE 4096ULL
#define VMALLOC_END 0xffffe00000000000ULL

_Noreturn void kernel_halt(void) { abort(); }
void klog(const char *message) { (void)message; }

static struct {
	uint64_t virtual_address;
	uint64_t physical_address;
	bool active;
} mapped[32];
static size_t map_calls;
static size_t fail_map_at;

uint64_t vmm_vmalloc_device_start(void) { return VMALLOC_END - VMM_DEVICE_WINDOW_SIZE; }
uint64_t vmm_vmalloc_device_end(void) { return VMALLOC_END; }
bool vmm_physical_range_mmio_safe(uint64_t address, uint64_t length)
{
	return address == 0xfebf0000ULL && length != 0;
}
struct address_space *vmm_kernel_address_space(void)
{
	return (struct address_space *)(uintptr_t)1;
}
bool vmm_map_page(struct address_space *space, uint64_t virtual_address,
	uint64_t physical_address, unsigned int flags)
{
	(void)space;
	assert(flags == (VMM_WRITE | VMM_UNCACHED));
	map_calls++;
	if (fail_map_at != 0 && map_calls == fail_map_at)
		return false;
	for (size_t i = 0; i < sizeof(mapped) / sizeof(mapped[0]); i++) {
		if (!mapped[i].active) {
			mapped[i].virtual_address = virtual_address;
			mapped[i].physical_address = physical_address;
			mapped[i].active = true;
			return true;
		}
	}
	return false;
}
bool vmm_unmap_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address)
{
	(void)space;
	for (size_t i = 0; i < sizeof(mapped) / sizeof(mapped[0]); i++) {
		if (mapped[i].active && mapped[i].virtual_address == virtual_address) {
			*physical_address = mapped[i].physical_address;
			mapped[i].active = false;
			return true;
		}
	}
	return false;
}

static void
test_ranges(void)
{
	struct pci_bar_resource bar = {
		.type = PCI_BAR_MEMORY32,
		.base = 0xfebf0000,
		.aperture_size = 0x10000,
		.slots = 1
	};
	struct pci_mmio_range range;

	assert(pci_bar_resource_valid(&bar));
	assert(pci_mmio_range_calculate(&bar, 0x123, 0x100, &range));
	assert(range.physical_page_base == 0xfebf0000);
	assert(range.page_count == 1);
	assert(range.resource_offset == 0x123);
	assert(!pci_mmio_range_calculate(&bar, 0xffff, 2, &range));
	assert(!pci_mmio_range_calculate(&bar, 0, 0, &range));
	bar.aperture_size = 0;
	assert(!pci_bar_resource_valid(&bar));
	bar.aperture_size = 0x1800;
	assert(!pci_bar_resource_valid(&bar));
	bar.aperture_size = 0x10000;
	bar.base = 0xfebf1000;
	assert(!pci_bar_resource_valid(&bar));
	bar.base = 0xffff1000;
	assert(!pci_bar_resource_valid(&bar));
	bar.type = PCI_BAR_MEMORY64;
	bar.slots = 1;
	bar.base = UINT64_MAX - 0xfff;
	assert(!pci_bar_resource_valid(&bar));
	bar.base = 0x100000000ULL;
	bar.slots = 2;
	assert(pci_bar_resource_valid(&bar));
	assert(pci_mmio_range_calculate(&bar, 0x1000, PAGE, &range));
	assert(range.physical_page_base == 0x100001000ULL);
	assert(range.page_count == 1);
	bar.base = 0x10000010ULL;
	bar.aperture_size = 16;
	assert(pci_mmio_range_calculate(&bar, 0, 16, &range));
	assert(range.physical_page_base == 0x10000000ULL);
	assert(range.page_count == 1 && range.resource_offset == 16);
}

static void
test_leases(void)
{
	struct pci_bar_resource bar = {
		.type = PCI_BAR_MEMORY32,
		.base = 0xfebf0000,
		.aperture_size = 0x10000,
		.slots = 1
	};
	struct pci_mmio_lease lease = {0};
	struct pci_mmio_lease failed = {0};
	volatile void *address;

	map_calls = fail_map_at = 0;
	assert(pci_mmio_map(&bar, 0x20, 32, &lease));
	assert(pci_mmio_lease_address(&lease, &address));
	assert((uintptr_t)address ==
	    (uintptr_t)(VMALLOC_END - VMM_DEVICE_WINDOW_SIZE + 0x20));
	assert(pci_mmio_release(&lease));
	assert(!pci_mmio_release(&lease));
	assert(!pci_mmio_lease_address(&lease, &address));

	/* Mapping the second page fails; the first page must be unwound. */
	fail_map_at = map_calls + 2;
	bar.aperture_size = 0x10000;
	assert(!pci_mmio_map(&bar, 0, 2 * PAGE, &failed));
	for (size_t i = 0; i < sizeof(mapped) / sizeof(mapped[0]); i++)
		assert(!mapped[i].active);
	fail_map_at = 0;
}

int
main(void)
{
	test_ranges();
	test_leases();
	puts("PCI resource range and MMIO lease tests passed");
	return 0;
}
