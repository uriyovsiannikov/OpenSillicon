#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/dma_ring.h"
#include "kernel/dma.h"

#define TEST_PHYSICAL 0x300000ULL

static _Alignas(4096) uint8_t backing_memory[4096];
static bool backing_allocated;

bool
pmm_alloc_contiguous(size_t pages, size_t alignment_pages,
	uint64_t maximum_address_exclusive, uint64_t *physical)
{
	if (backing_allocated || pages != 1 || alignment_pages == 0 ||
	    TEST_PHYSICAL % (alignment_pages * 4096ULL) != 0 ||
	    maximum_address_exclusive <= TEST_PHYSICAL + 4096 || physical == NULL)
		return false;
	backing_allocated = true;
	*physical = TEST_PHYSICAL;
	return true;
}

bool
pmm_free_contiguous(uint64_t physical, size_t pages)
{
	if (!backing_allocated || physical != TEST_PHYSICAL || pages != 1)
		return false;
	backing_allocated = false;
	return true;
}

uint64_t pmm_total_pages(void) { return 64; }
uint64_t vmm_hhdm_offset(void)
{
	return (uint64_t)(uintptr_t)backing_memory - TEST_PHYSICAL;
}

int
main(void)
{
	uint8_t owners[4];
	struct dma_descriptor_ring ring;
	struct dma_buffer backing;
	enum dma_ring_owner owner;
	uint64_t device_address;
	const struct dma_boot_contract contract = {
		.requested_revision_honored = true,
		.loaded_base_revision = 5,
		.keep_iommu_requested = false
	};

	assert(dma_init(&contract));
	assert(dma_alloc(sizeof(backing_memory), 4096, 32, &backing));
	assert(!dma_ring_init(NULL, owners, 4, backing, 64));
	assert(!dma_ring_init(&ring, owners, 0, backing, 64));
	assert(!dma_ring_init(&ring, owners, 4, backing, 4097));
	assert(dma_ring_init(&ring, owners, 4, backing, 64));
	assert(!ring.active);
	assert(!dma_ring_publish(&ring, 0));
	assert(dma_ring_start(&ring));
	assert(!dma_ring_start(&ring));
	assert(!dma_free(backing));
	assert(!dma_device_acquire(backing, &device_address));
	assert(dma_ring_publish(&ring, 0));
	assert(dma_ring_publish(&ring, 1));
	assert(!dma_ring_publish(&ring, 0));
	assert(!dma_ring_publish(&ring, 4));
	assert(!dma_ring_reclaim(&ring, 0, false));
	assert(dma_ring_owner_get(&ring, 0, &owner));
	assert(owner == DMA_RING_DEVICE_OWNED);
	assert(dma_ring_reclaim(&ring, 0, true));
	assert(!dma_ring_reclaim(&ring, 0, true));
	assert(dma_ring_owner_get(&ring, 0, &owner));
	assert(owner == DMA_RING_CPU_OWNED);
	assert(!dma_ring_quiesce(&ring, false));
	assert(dma_ring_publish(&ring, 2));
	assert(!dma_ring_owner_get(&ring, 8, &owner));
	assert(dma_ring_quiesce(&ring, true));
	assert(!ring.active);
	assert(dma_ring_owner_get(&ring, 1, &owner));
	assert(owner == DMA_RING_CPU_OWNED);
	assert(!dma_ring_quiesce(&ring, true));
	assert(dma_ring_start(&ring));
	assert(dma_ring_publish(&ring, 3));
	assert(dma_ring_quiesce(&ring, true));
	assert(dma_free(backing));
	puts("DMA descriptor-ring ownership and quiescence tests passed");
	return 0;
}
