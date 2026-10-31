#include "kernel/dma_ring.h"

static void
dma_write_barrier(void)
{
	/* Order descriptor stores before the subsequent MMIO tail/doorbell store.
	 * This is not cache maintenance and assumes coherent x86 DMA memory. */
	__asm__ volatile ("sfence" : : : "memory");
}

static void
dma_completion_barrier(void)
{
	/* Completion was observed in device memory; order subsequent CPU reads of
	 * descriptor fields and packet data after that observation. */
	__asm__ volatile ("mfence" : : : "memory");
}

bool
dma_ring_init(struct dma_descriptor_ring *ring, uint8_t *owner_storage,
	size_t descriptor_count, struct dma_buffer backing,
	size_t descriptor_storage_bytes)
{
	struct dma_buffer_info info;

	if (ring == NULL || owner_storage == NULL || descriptor_count == 0 ||
	    descriptor_storage_bytes == 0 ||
	    !dma_buffer_query(backing, &info) ||
	    descriptor_storage_bytes > info.allocated_size)
		return false;
	for (size_t i = 0; i < descriptor_count; i++)
		owner_storage[i] = DMA_RING_CPU_OWNED;
	*ring = (struct dma_descriptor_ring){
		.owners = owner_storage,
		.count = descriptor_count,
		.backing = backing,
		.active = false
	};
	return true;
}

bool
dma_ring_start(struct dma_descriptor_ring *ring)
{
	if (ring == NULL || ring->owners == NULL || ring->count == 0 ||
	    ring->active)
		return false;
	if (!dma_descriptor_ring_pin(ring->backing))
		return false;
	dma_write_barrier();
	ring->active = true;
	return true;
}

bool
dma_ring_publish(struct dma_descriptor_ring *ring, size_t index)
{
	if (ring == NULL || ring->owners == NULL || !ring->active ||
	    index >= ring->count || ring->owners[index] != DMA_RING_CPU_OWNED)
		return false;
	dma_write_barrier();
	ring->owners[index] = DMA_RING_DEVICE_OWNED;
	return true;
}

bool
dma_ring_reclaim(struct dma_descriptor_ring *ring, size_t index,
	bool hardware_completion_observed)
{
	if (ring == NULL || ring->owners == NULL || !ring->active ||
	    index >= ring->count ||
	    ring->owners[index] != DMA_RING_DEVICE_OWNED ||
	    !hardware_completion_observed)
		return false;
	dma_completion_barrier();
	ring->owners[index] = DMA_RING_CPU_OWNED;
	return true;
}

bool
dma_ring_owner_get(const struct dma_descriptor_ring *ring, size_t index,
	enum dma_ring_owner *owner)
{
	if (ring == NULL || ring->owners == NULL || owner == NULL ||
	    index >= ring->count ||
	    ring->owners[index] > DMA_RING_DEVICE_OWNED)
		return false;
	*owner = (enum dma_ring_owner)ring->owners[index];
	return true;
}

bool
dma_ring_quiesce(struct dma_descriptor_ring *ring, bool hardware_stopped)
{
	if (ring == NULL || ring->owners == NULL || ring->count == 0 ||
	    !ring->active || !hardware_stopped)
		return false;
	dma_completion_barrier();
	if (!dma_descriptor_ring_unpin(ring->backing))
		return false;
	for (size_t i = 0; i < ring->count; i++)
		ring->owners[i] = DMA_RING_CPU_OWNED;
	ring->active = false;
	return true;
}
