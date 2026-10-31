#include <stdint.h>

#include "kernel/dma.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/vmm.h"

#define DMA_PAGE_SIZE 4096ULL

struct dma_allocation {
	uint64_t generation;
	uint64_t physical_address;
	void *cpu_address;
	size_t requested_size;
	size_t allocated_size;
	size_t page_count;
	enum dma_owner owner;
	bool descriptor_ring_pinned;
	bool active;
};

static struct dma_allocation allocations[DMA_ALLOCATION_SLOTS];
static bool initialized;
static enum dma_addressing_mode addressing_mode;

static bool
power_of_two(size_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static struct dma_allocation *
lookup(struct dma_buffer buffer)
{
	struct dma_allocation *allocation;

	if (addressing_mode != DMA_ADDRESSING_PHYSICAL_IDENTITY ||
	    buffer.slot >= DMA_ALLOCATION_SLOTS ||
	    buffer.generation == 0)
		return NULL;
	allocation = &allocations[buffer.slot];
	if (!allocation->active || allocation->generation != buffer.generation)
		return NULL;
	return allocation;
}

bool
dma_boot_contract_allows_identity(const struct dma_boot_contract *contract)
{
	return contract != NULL && contract->requested_revision_honored &&
	    contract->loaded_base_revision >= DMA_REQUIRED_LIMINE_BASE_REVISION &&
	    !contract->keep_iommu_requested;
}

bool
dma_init(const struct dma_boot_contract *contract)
{
	if (initialized || contract == NULL || vmm_hhdm_offset() == 0 ||
	    pmm_total_pages() == 0)
		return false;
	initialized = true;
	addressing_mode = dma_boot_contract_allows_identity(contract) ?
	    DMA_ADDRESSING_PHYSICAL_IDENTITY : DMA_ADDRESSING_UNAVAILABLE;
	return true;
}

enum dma_addressing_mode
dma_get_addressing_mode(void)
{
	return addressing_mode;
}

bool
dma_identity_addressing_available(void)
{
	return addressing_mode == DMA_ADDRESSING_PHYSICAL_IDENTITY;
}

bool
dma_alloc(size_t size, size_t alignment, unsigned int address_bits,
	struct dma_buffer *buffer)
{
	struct dma_allocation *allocation = NULL;
	uint64_t address_limit;
	uint64_t physical;
	uint64_t hhdm;
	size_t allocated_size;
	size_t page_count;
	size_t alignment_pages;
	size_t i;

	if (!dma_identity_addressing_available() || buffer == NULL || size == 0 ||
	    size > DMA_MAX_ALLOCATION_SIZE || !power_of_two(alignment) ||
	    address_bits == 0 || address_bits > 64 ||
	    size > SIZE_MAX - (DMA_PAGE_SIZE - 1))
		return false;
	if (alignment < DMA_PAGE_SIZE)
		alignment = DMA_PAGE_SIZE;
	if (alignment % DMA_PAGE_SIZE != 0)
		return false;
	alignment_pages = alignment / DMA_PAGE_SIZE;
	allocated_size = (size + DMA_PAGE_SIZE - 1) & ~(size_t)(DMA_PAGE_SIZE - 1);
	page_count = allocated_size / DMA_PAGE_SIZE;
	address_limit = address_bits == 64 ? UINT64_MAX : 1ULL << address_bits;
	for (i = 0; i < DMA_ALLOCATION_SLOTS; i++) {
		if (!allocations[i].active) {
			allocation = &allocations[i];
			break;
		}
	}
	if (allocation == NULL || !pmm_alloc_contiguous(page_count,
	    alignment_pages, address_limit, &physical))
		return false;
	hhdm = vmm_hhdm_offset();
	if (hhdm > UINT64_MAX - physical ||
	    allocated_size - 1 > UINTPTR_MAX - (uintptr_t)(hhdm + physical)) {
		(void)pmm_free_contiguous(physical, page_count);
		return false;
	}
	allocation->generation++;
	if (allocation->generation == 0)
		allocation->generation++;
	allocation->physical_address = physical;
	allocation->cpu_address = (void *)(uintptr_t)(hhdm + physical);
	allocation->requested_size = size;
	allocation->allocated_size = allocated_size;
	allocation->page_count = page_count;
	allocation->owner = DMA_OWNER_CPU;
	allocation->descriptor_ring_pinned = false;
	allocation->active = true;
	{
		volatile uint8_t *bytes = allocation->cpu_address;
		for (size_t byte = 0; byte < allocated_size; byte++)
			bytes[byte] = 0;
	}
	*buffer = (struct dma_buffer){ .slot = (uint16_t)i,
	    .generation = allocation->generation };
	return true;
}

bool
dma_buffer_query(struct dma_buffer buffer, struct dma_buffer_info *info)
{
	struct dma_allocation *allocation = lookup(buffer);

	if (allocation == NULL || info == NULL ||
	    allocation->owner != DMA_OWNER_CPU)
		return false;
	*info = (struct dma_buffer_info){
		.cpu_virtual_address = allocation->cpu_address,
		.physical_address = allocation->physical_address,
		.device_address = allocation->physical_address,
		.requested_size = allocation->requested_size,
		.allocated_size = allocation->allocated_size,
		.page_count = allocation->page_count,
		.owner = allocation->owner
	};
	return true;
}

bool
dma_device_acquire(struct dma_buffer buffer, uint64_t *device_address)
{
	struct dma_allocation *allocation = lookup(buffer);

	if (allocation == NULL || device_address == NULL ||
	    allocation->owner != DMA_OWNER_CPU ||
	    allocation->descriptor_ring_pinned)
		return false;
	/* The validated Limine revision excludes IOMMU translation. This fence
	 * orders coherent x86 DMA; it is not cache maintenance. Non-coherent
	 * platforms are outside the currently supported profile. */
	__asm__ volatile ("mfence" : : : "memory");
	allocation->owner = DMA_OWNER_DEVICE;
	*device_address = allocation->physical_address;
	return true;
}

bool
dma_device_complete(struct dma_buffer buffer)
{
	struct dma_allocation *allocation = lookup(buffer);

	if (allocation == NULL || allocation->owner != DMA_OWNER_DEVICE ||
	    allocation->descriptor_ring_pinned)
		return false;
	/* The caller must first establish hardware completion. */
	__asm__ volatile ("mfence" : : : "memory");
	allocation->owner = DMA_OWNER_CPU;
	return true;
}

bool
dma_descriptor_ring_pin(struct dma_buffer buffer)
{
	struct dma_allocation *allocation = lookup(buffer);

	if (allocation == NULL || allocation->owner != DMA_OWNER_CPU ||
	    allocation->descriptor_ring_pinned)
		return false;
	__asm__ volatile ("mfence" : : : "memory");
	allocation->descriptor_ring_pinned = true;
	return true;
}

bool
dma_descriptor_ring_unpin(struct dma_buffer buffer)
{
	struct dma_allocation *allocation = lookup(buffer);

	if (allocation == NULL || allocation->owner != DMA_OWNER_CPU ||
	    !allocation->descriptor_ring_pinned)
		return false;
	__asm__ volatile ("mfence" : : : "memory");
	allocation->descriptor_ring_pinned = false;
	return true;
}

bool
dma_free(struct dma_buffer buffer)
{
	struct dma_allocation *allocation = lookup(buffer);

	if (allocation == NULL || allocation->owner != DMA_OWNER_CPU ||
	    allocation->descriptor_ring_pinned)
		return false;
	__asm__ volatile ("mfence" : : : "memory");
	if (!pmm_free_contiguous(allocation->physical_address,
	    allocation->page_count))
		return false;
	allocation->cpu_address = NULL;
	allocation->physical_address = 0;
	allocation->requested_size = 0;
	allocation->allocated_size = 0;
	allocation->page_count = 0;
	allocation->owner = 0;
	allocation->descriptor_ring_pinned = false;
	allocation->active = false;
	return true;
}
