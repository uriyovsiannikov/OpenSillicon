#ifndef KERNEL_DMA_RING_H
#define KERNEL_DMA_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/dma.h"

enum dma_ring_owner {
	DMA_RING_CPU_OWNED = 0,
	DMA_RING_DEVICE_OWNED
};

/* Software ownership metadata is separate from the hardware descriptor ring.
 * The caller owns both arrays and must keep them allocated until the device is
 * confirmed stopped. The initial runtime is single-CPU. */
struct dma_descriptor_ring {
	uint8_t *owners;
	size_t count;
	struct dma_buffer backing;
	bool active;
};

bool dma_ring_init(struct dma_descriptor_ring *ring, uint8_t *owner_storage,
	size_t descriptor_count, struct dma_buffer backing,
	size_t descriptor_storage_bytes);
bool dma_ring_start(struct dma_descriptor_ring *ring);
/* Call only after fully preparing the descriptor, including its device
 * ownership/status field, and before updating a device tail/doorbell. */
bool dma_ring_publish(struct dma_descriptor_ring *ring, size_t index);
/* Call after observing the hardware completion/ownership bit. A false
 * completion leaves ownership unchanged. */
bool dma_ring_reclaim(struct dma_descriptor_ring *ring, size_t index,
	bool hardware_completion_observed);
bool dma_ring_owner_get(const struct dma_descriptor_ring *ring, size_t index,
	enum dma_ring_owner *owner);
/* hardware_stopped must mean device-specific reset/idle was verified. */
bool dma_ring_quiesce(struct dma_descriptor_ring *ring,
	bool hardware_stopped);

#endif
