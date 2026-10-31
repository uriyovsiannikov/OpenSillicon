#ifndef KERNEL_DMA_H
#define KERNEL_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DMA_MAX_ALLOCATION_SIZE (16U * 1024U * 1024U)
#define DMA_ALLOCATION_SLOTS 64U
#define DMA_REQUIRED_LIMINE_BASE_REVISION 5U

enum dma_owner {
	DMA_OWNER_CPU = 1,
	DMA_OWNER_DEVICE = 2
};

/* Opaque, generation-tagged handle. Copying a stale handle cannot release a
 * later allocation that reused the same registry slot. */
struct dma_buffer {
	uint16_t slot;
	uint64_t generation;
};

struct dma_buffer_info {
	/* CPU virtual mapping through the Limine HHDM. */
	void *cpu_virtual_address;
	/* Physical frame address allocated by PMM. */
	uint64_t physical_address;
	/* Device-visible address. Currently identical to physical_address only
	 * under the validated identity-address DMA boot contract. */
	uint64_t device_address;
	size_t requested_size;
	size_t allocated_size;
	size_t page_count;
	enum dma_owner owner;
};

struct dma_boot_contract {
	bool requested_revision_honored;
	uint64_t loaded_base_revision;
	bool keep_iommu_requested;
};

enum dma_addressing_mode {
	DMA_ADDRESSING_UNAVAILABLE = 0,
	DMA_ADDRESSING_PHYSICAL_IDENTITY
};

/* Limine base revision 5+ disables IOMMU translation unless KEEP_IOMMU was
 * requested. This policy does not claim IOMMU translation support. */
bool dma_boot_contract_allows_identity(
	const struct dma_boot_contract *contract);
/* Initializes the subsystem even when the contract is insufficient; in that
 * case DMA remains unavailable and allocations fail closed. */
bool dma_init(const struct dma_boot_contract *contract);
enum dma_addressing_mode dma_get_addressing_mode(void);
bool dma_identity_addressing_available(void);
bool dma_alloc(size_t size, size_t alignment, unsigned int address_bits,
	struct dma_buffer *buffer);
bool dma_buffer_query(struct dma_buffer buffer, struct dma_buffer_info *info);
bool dma_device_acquire(struct dma_buffer buffer, uint64_t *device_address);
bool dma_device_complete(struct dma_buffer buffer);
/* Used only by the descriptor-ring owner; the allocation cannot be freed or
 * handed off as a whole buffer while pinned. */
bool dma_descriptor_ring_pin(struct dma_buffer buffer);
bool dma_descriptor_ring_unpin(struct dma_buffer buffer);
bool dma_free(struct dma_buffer buffer);

#endif
