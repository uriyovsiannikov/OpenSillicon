#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/dma.h"
#include "kernel/vmm.h"

#define TEST_PHYSICAL 0x200000ULL

static _Alignas(4096) uint8_t storage[DMA_MAX_ALLOCATION_SIZE];
static bool fail_allocation;
static bool allocated;
static size_t allocated_pages;
static size_t freed_pages;

bool pmm_alloc_contiguous(size_t pages, size_t alignment_pages,
	uint64_t maximum_address_exclusive, uint64_t *physical)
{
	if (fail_allocation || physical == NULL || pages == 0 ||
	    alignment_pages == 0 ||
	    TEST_PHYSICAL % (alignment_pages * 4096ULL) != 0 ||
	    maximum_address_exclusive <= TEST_PHYSICAL + pages * 4096ULL)
		return false;
	allocated = true;
	allocated_pages = pages;
	*physical = TEST_PHYSICAL;
	return true;
}
bool pmm_free_contiguous(uint64_t physical, size_t pages)
{
	if (!allocated || physical != TEST_PHYSICAL || pages != allocated_pages)
		return false;
	allocated = false;
	freed_pages = pages;
	return true;
}
uint64_t pmm_total_pages(void) { return 1024; }
uint64_t vmm_hhdm_offset(void)
{
	return (uint64_t)(uintptr_t)storage - TEST_PHYSICAL;
}

static void
test_dma_lifecycle(void)
{
	struct dma_buffer buffer;
	struct dma_buffer_info info;
	uint64_t device_address;

	assert(!dma_alloc(0, 4096, 32, &buffer));
	assert(!dma_alloc(8192, 4096, 20, &buffer));
	assert(dma_alloc(5000, 8192, 32, &buffer));
	assert(dma_buffer_query(buffer, &info));
	assert(info.cpu_virtual_address == storage);
	assert(info.physical_address == TEST_PHYSICAL);
	assert(info.device_address == TEST_PHYSICAL);
	assert(info.requested_size == 5000 && info.allocated_size == 8192);
	assert(info.page_count == 2 && info.owner == DMA_OWNER_CPU);
	for (size_t i = 0; i < info.allocated_size; i++)
		assert(((uint8_t *)info.cpu_virtual_address)[i] == 0);
	assert(dma_device_acquire(buffer, &device_address));
	assert(device_address == TEST_PHYSICAL);
	assert(!dma_device_acquire(buffer, &device_address));
	assert(!dma_free(buffer));
	assert(dma_device_complete(buffer));
	assert(!dma_device_complete(buffer));
	assert(dma_buffer_query(buffer, &info) && info.owner == DMA_OWNER_CPU);
	assert(dma_free(buffer) && freed_pages == 2);
	assert(!dma_free(buffer));
	assert(!dma_buffer_query(buffer, &info));

	fail_allocation = true;
	assert(!dma_alloc(4096, 4096, 32, &buffer));
	fail_allocation = false;
}

static void
test_boot_contract(void)
{
	const struct dma_boot_contract accepted = {
		.requested_revision_honored = true,
		.loaded_base_revision = 5,
		.keep_iommu_requested = false
	};
	const struct dma_boot_contract newer = {
		.requested_revision_honored = true,
		.loaded_base_revision = 6,
		.keep_iommu_requested = false
	};
	const struct dma_boot_contract too_old = {
		.requested_revision_honored = true,
		.loaded_base_revision = 4,
		.keep_iommu_requested = false
	};
	const struct dma_boot_contract not_honored = {
		.requested_revision_honored = false,
		.loaded_base_revision = 5,
		.keep_iommu_requested = false
	};
	const struct dma_boot_contract iommu_preserved = {
		.requested_revision_honored = true,
		.loaded_base_revision = 5,
		.keep_iommu_requested = true
	};

	assert(dma_boot_contract_allows_identity(&accepted));
	assert(dma_boot_contract_allows_identity(&newer));
	assert(!dma_boot_contract_allows_identity(&too_old));
	assert(!dma_boot_contract_allows_identity(&not_honored));
	assert(!dma_boot_contract_allows_identity(&iommu_preserved));
	assert(!dma_boot_contract_allows_identity(NULL));
}

int
main(int argc, char **argv)
{
	const struct dma_boot_contract accepted = {
		.requested_revision_honored = true,
		.loaded_base_revision = 5,
		.keep_iommu_requested = false
	};
	const struct dma_boot_contract rejected = {
		.requested_revision_honored = false,
		.loaded_base_revision = 4,
		.keep_iommu_requested = false
	};
	struct dma_buffer unavailable_buffer;

	(void)argv;
	test_boot_contract();
	if (argc > 1) {
		assert(dma_init(&rejected));
		assert(dma_get_addressing_mode() == DMA_ADDRESSING_UNAVAILABLE);
		assert(!dma_identity_addressing_available());
		assert(!dma_alloc(4096, 4096, 32, &unavailable_buffer));
		puts("DMA boot contract rejection and fail-closed allocation passed");
		return 0;
	}
	assert(dma_init(&accepted));
	assert(dma_get_addressing_mode() == DMA_ADDRESSING_PHYSICAL_IDENTITY);
	test_dma_lifecycle();
	puts("DMA boot contract, validation, zero-fill, and ownership tests passed");
	return 0;
}
