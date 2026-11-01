#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/virtio_rng.h"

#define FAKE_DMA_MAX 4
#define IO_BASE 0xc000U
#define Q_ALIGN 4096U

struct fake_dma {
	struct dma_buffer handle;
	struct dma_buffer_info info;
	bool active;
	bool pinned;
	bool device_owned;
};

struct fake_transport {
	struct fake_dma dma[FAKE_DMA_MAX];
	uint64_t next_address;
	uint16_t pci_command;
	uint16_t queue_size;
	uint32_t queue_pfn;
	uint32_t guest_features;
	uint8_t device_status;
	uint16_t used_index;
	uint32_t completion_id;
	uint32_t completion_length;
	bool complete_requests;
	bool fail_reset;
	uint16_t completion_advance;
	unsigned int allocations;
	unsigned int frees;
	unsigned int acquires;
	unsigned int completes;
};

struct fake_desc {
	uint64_t address;
	uint32_t length;
	uint16_t flags;
	uint16_t next;
};

struct fake_used_element {
	uint32_t id;
	uint32_t length;
};

static struct fake_dma *
find_dma(struct fake_transport *fake, uint64_t physical)
{
	for (size_t i = 0; i < FAKE_DMA_MAX; i++) {
		if (fake->dma[i].active &&
		    fake->dma[i].info.physical_address == physical)
			return &fake->dma[i];
	}
	return NULL;
}

static bool
fake_pci_read(void *context, const struct pci_device_info *device,
	uint16_t offset, unsigned int width, uint32_t *value)
{
	struct fake_transport *fake = context;
	(void)device;
	if (value == NULL || offset != 4 || width != 16)
		return false;
	*value = fake->pci_command;
	return true;
}

static bool
fake_pci_write(void *context, const struct pci_device_info *device,
	uint16_t offset, unsigned int width, uint32_t value)
{
	struct fake_transport *fake = context;
	(void)device;
	if (offset != 4 || width != 16)
		return false;
	fake->pci_command = (uint16_t)value;
	return true;
}

static uint8_t
fake_in8(void *context, uint16_t port)
{
	struct fake_transport *fake = context;
	if (port == IO_BASE + 0x12)
		return fake->device_status;
	return 0;
}

static uint16_t
fake_in16(void *context, uint16_t port)
{
	struct fake_transport *fake = context;
	if (port == IO_BASE + 0x0c)
		return fake->queue_size;
	if (port == IO_BASE + 0x0e)
		return 0;
	return 0;
}

static uint32_t
fake_in32(void *context, uint16_t port)
{
	struct fake_transport *fake = context;
	if (port == IO_BASE + 0x08)
		return fake->queue_pfn;
	return 0x0000ffffU;
}

static void
fake_out8(void *context, uint16_t port, uint8_t value)
{
	struct fake_transport *fake = context;
	if (port != IO_BASE + 0x12)
		return;
	if (value == 0 && fake->fail_reset)
		return;
	fake->device_status = value;
}

static void
fake_out16(void *context, uint16_t port, uint16_t value)
{
	struct fake_transport *fake = context;
	if (port == IO_BASE + 0x10 && value == 0 && fake->complete_requests) {
		struct fake_dma *queue = find_dma(fake,
		    (uint64_t)fake->queue_pfn * Q_ALIGN);
		struct fake_desc *descriptors;
		uint8_t *available_index;
		uint16_t *used_index;
		struct fake_used_element *used;
		struct fake_dma *data;
		if (queue == NULL)
			abort();
		descriptors = queue->info.cpu_virtual_address;
		data = find_dma(fake, descriptors[0].address);
		if (data == NULL || (descriptors[0].flags & 2U) == 0 ||
		    descriptors[0].length > data->info.requested_size)
			abort();
		memset(data->info.cpu_virtual_address, 0x5a,
		    descriptors[0].length);
		available_index = (uint8_t *)queue->info.cpu_virtual_address +
		    (size_t)fake->queue_size * sizeof(struct fake_desc) + 2;
		(void)available_index;
		used = (struct fake_used_element *)((uint8_t *)
		    queue->info.cpu_virtual_address + Q_ALIGN + 4);
		used_index = (uint16_t *)((uint8_t *)queue->info.cpu_virtual_address +
		    Q_ALIGN + 2);
		used[*used_index % fake->queue_size].id = fake->completion_id;
		used[*used_index % fake->queue_size].length =
		    fake->completion_length;
	*used_index = (uint16_t)(*used_index +
	    (fake->completion_advance == 0 ? 1 : fake->completion_advance));
		fake->used_index = *used_index;
	}
}

static void
fake_out32(void *context, uint16_t port, uint32_t value)
{
	struct fake_transport *fake = context;
	if (port == IO_BASE + 0x08)
		fake->queue_pfn = value;
	if (port == IO_BASE + 0x04)
		fake->guest_features = value;
}

static bool
fake_dma_alloc(void *context, size_t size, size_t alignment,
	unsigned int address_bits, struct dma_buffer *buffer,
	struct dma_buffer_info *info)
{
	struct fake_transport *fake = context;
	struct fake_dma *slot = NULL;
	void *memory = NULL;
	(void)address_bits;
	for (size_t i = 0; i < FAKE_DMA_MAX; i++)
		if (!fake->dma[i].active) {
			slot = &fake->dma[i];
			break;
		}
	if (slot == NULL || posix_memalign(&memory, alignment, size) != 0)
		return false;
	memset(memory, 0, size);
	*slot = (struct fake_dma){0};
	slot->handle = (struct dma_buffer){
		.slot = (uint16_t)(slot - fake->dma), .generation = 1 };
	slot->info = (struct dma_buffer_info){
		.cpu_virtual_address = memory,
		.physical_address = fake->next_address,
		.device_address = fake->next_address,
		.requested_size = size,
		.allocated_size = size,
		.page_count = (size + 4095) / 4096,
		.owner = DMA_OWNER_CPU
	};
	fake->next_address += (size + 4095) & ~(uint64_t)4095;
	slot->active = true;
	*buffer = slot->handle;
	*info = slot->info;
	fake->allocations++;
	return true;
}

static bool
fake_dma_acquire(void *context, struct dma_buffer buffer)
{
	struct fake_transport *fake = context;
	if (buffer.slot >= FAKE_DMA_MAX || !fake->dma[buffer.slot].active ||
	    fake->dma[buffer.slot].handle.generation != buffer.generation ||
	    fake->dma[buffer.slot].device_owned)
		return false;
	fake->dma[buffer.slot].device_owned = true;
	fake->acquires++;
	return true;
}

static bool
fake_dma_complete(void *context, struct dma_buffer buffer)
{
	struct fake_transport *fake = context;
	if (buffer.slot >= FAKE_DMA_MAX || !fake->dma[buffer.slot].active ||
	    fake->dma[buffer.slot].handle.generation != buffer.generation ||
	    !fake->dma[buffer.slot].device_owned)
		return false;
	fake->dma[buffer.slot].device_owned = false;
	fake->completes++;
	return true;
}

static bool
fake_dma_pin(void *context, struct dma_buffer buffer)
{
	struct fake_transport *fake = context;
	if (buffer.slot >= FAKE_DMA_MAX || !fake->dma[buffer.slot].active)
		return false;
	fake->dma[buffer.slot].pinned = true;
	return true;
}

static bool
fake_dma_unpin(void *context, struct dma_buffer buffer)
{
	struct fake_transport *fake = context;
	if (buffer.slot >= FAKE_DMA_MAX || !fake->dma[buffer.slot].active ||
	    !fake->dma[buffer.slot].pinned)
		return false;
	fake->dma[buffer.slot].pinned = false;
	return true;
}

static bool
fake_dma_free(void *context, struct dma_buffer buffer)
{
	struct fake_transport *fake = context;
	struct fake_dma *slot;
	if (buffer.slot >= FAKE_DMA_MAX || !fake->dma[buffer.slot].active)
		return false;
	slot = &fake->dma[buffer.slot];
	if (slot->handle.generation != buffer.generation || slot->pinned ||
	    slot->device_owned)
		return false;
	free(slot->info.cpu_virtual_address);
	*slot = (struct fake_dma){0};
	fake->frees++;
	return true;
}

static const struct virtio_rng_test_ops ops = {
	.pci_read = fake_pci_read,
	.pci_write = fake_pci_write,
	.in8 = fake_in8,
	.in16 = fake_in16,
	.in32 = fake_in32,
	.out8 = fake_out8,
	.out16 = fake_out16,
	.out32 = fake_out32,
	.dma_alloc = fake_dma_alloc,
	.dma_acquire = fake_dma_acquire,
	.dma_complete = fake_dma_complete,
	.dma_pin = fake_dma_pin,
	.dma_unpin = fake_dma_unpin,
	.dma_free = fake_dma_free
};

static struct pci_device_info
fake_device(void)
{
	struct pci_device_info device = {0};
	device.vendor_id = VIRTIO_RNG_DEVICE_VENDOR;
	device.device_id = VIRTIO_RNG_LEGACY_DEVICE;
	device.bars[0] = IO_BASE | 1U;
	return device;
}

static void
test_success_and_shutdown(void)
{
	struct fake_transport fake = {
		.next_address = 0x100000,
		.queue_size = 8,
		.completion_length = 19,
		.complete_requests = true
	};
	struct pci_device_info device = fake_device();
	uint8_t output[VIRTIO_RNG_MAX_READ];
	size_t received = 99;
	assert(virtio_rng_test_init(&device, &ops, &fake));
	assert(!virtio_rng_test_init(&device, &ops, &fake));
	assert(virtio_rng_available());
	assert(fake.guest_features == 0);
	assert(virtio_rng_read(output, sizeof(output), &received) == VIRTIO_RNG_OK);
	assert(received == 19);
	for (size_t i = 0; i < received; i++)
		assert(output[i] == 0x5a);
	assert(fake.acquires == 1 && fake.completes == 1);
	for (unsigned int request = 0; request < 10; request++) {
		received = 99;
		assert(virtio_rng_read(output, 32, &received) == VIRTIO_RNG_OK);
		assert(received == 19);
	}
	assert(fake.acquires == 11 && fake.completes == 11);
	assert(virtio_rng_read(NULL, 0, &received) == VIRTIO_RNG_OK &&
	    received == 0);
	assert(virtio_rng_read(output, VIRTIO_RNG_MAX_READ + 1, &received) ==
	    VIRTIO_RNG_INVALID_ARGUMENT);
	assert(virtio_rng_shutdown() == VIRTIO_RNG_OK);
	assert(fake.allocations == 2 && fake.frees == 2);
	assert(fake.pci_command == 0);
	virtio_rng_test_reset_state();
}

static void
test_malformed_and_timeout(void)
{
	struct fake_transport fake = {
		.next_address = 0x200000,
		.queue_size = 8,
		.completion_id = 1,
		.completion_length = 8,
		.complete_requests = true
	};
	struct pci_device_info device = fake_device();
	uint8_t output[32];
	size_t received;
	memset(output, 0xcc, sizeof(output));
	assert(virtio_rng_test_init(&device, &ops, &fake));
	assert(virtio_rng_read(output, sizeof(output), &received) ==
	    VIRTIO_RNG_MALFORMED_COMPLETION);
	assert(received == 0 && fake.allocations == fake.frees);
	for (size_t i = 0; i < sizeof(output); i++)
		assert(output[i] == 0xcc);
	virtio_rng_test_reset_state();

	fake = (struct fake_transport){
		.next_address = 0x300000,
		.queue_size = 8,
		.complete_requests = false
	};
	assert(virtio_rng_test_init(&device, &ops, &fake));
	assert(virtio_rng_read(output, sizeof(output), &received) ==
	    VIRTIO_RNG_TIMEOUT);
	assert(received == 0 && fake.allocations == fake.frees);
	assert(!virtio_rng_available());
	virtio_rng_test_reset_state();
}

static void
test_bad_queue_and_unquiesced_dma(void)
{
	struct fake_transport fake = {
		.next_address = 0x400000,
		.queue_size = 3
	};
	struct pci_device_info device = fake_device();
	uint8_t output[8];
	size_t received;
	assert(!virtio_rng_test_init(&device, &ops, &fake));
	assert(fake.allocations == 0);
	virtio_rng_test_reset_state();

	fake = (struct fake_transport){
		.next_address = 0x480000,
		.queue_size = 8,
		.completion_length = 8,
		.completion_advance = 2,
		.complete_requests = true
	};
	assert(virtio_rng_test_init(&device, &ops, &fake));
	assert(virtio_rng_read(output, sizeof(output), &received) ==
	    VIRTIO_RNG_MALFORMED_COMPLETION);
	assert(received == 0 && fake.allocations == fake.frees);
	virtio_rng_test_reset_state();

	fake = (struct fake_transport){
		.next_address = 0x500000,
		.queue_size = 8,
		.complete_requests = false,
		.fail_reset = true
	};
	assert(virtio_rng_test_init(&device, &ops, &fake));
	assert(virtio_rng_read(output, sizeof(output), &received) ==
	    VIRTIO_RNG_TRANSPORT_FAILURE);
	assert(received == 0 && fake.allocations == 2 && fake.frees == 0);
	for (size_t i = 0; i < FAKE_DMA_MAX; i++) {
		if (fake.dma[i].active)
			free(fake.dma[i].info.cpu_virtual_address);
	}
	virtio_rng_test_reset_state();
}

int
main(void)
{
	struct pci_device_info device = fake_device();
	assert(virtio_rng_matches_device(&device));
	device.device_id++;
	assert(!virtio_rng_matches_device(&device));
	device = fake_device();
	device.bars[0] = 0;
	assert(virtio_rng_matches_device(&device));
	test_success_and_shutdown();
	test_malformed_and_timeout();
	test_bad_queue_and_unquiesced_dma();
	puts("VirtIO RNG legacy transport and bounded provider tests: PASS");
	return 0;
}
