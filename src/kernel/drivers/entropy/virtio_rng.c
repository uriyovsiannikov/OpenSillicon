#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/pci.h"
#include "kernel/dma.h"
#include "kernel/virtio_rng.h"

#define PCI_COMMAND_OFFSET 0x04U
#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_BUS_MASTER 0x0004U

#define VIRTIO_IO_HOST_FEATURES 0x00U
#define VIRTIO_IO_GUEST_FEATURES 0x04U
#define VIRTIO_IO_QUEUE_PFN 0x08U
#define VIRTIO_IO_QUEUE_SIZE 0x0cU
#define VIRTIO_IO_QUEUE_SELECT 0x0eU
#define VIRTIO_IO_QUEUE_NOTIFY 0x10U
#define VIRTIO_IO_DEVICE_STATUS 0x12U

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01U
#define VIRTIO_STATUS_DRIVER 0x02U
#define VIRTIO_STATUS_DRIVER_OK 0x04U
#define VIRTIO_STATUS_FAILED 0x80U

#define VIRTQ_DESC_F_WRITE 0x0002U
#define VIRTQ_ALIGN 4096U
#define VIRTIO_RNG_MAX_QUEUE 256U
#define VIRTIO_RNG_DATA_BYTES VIRTIO_RNG_MAX_READ

struct virtq_desc {
	uint64_t address;
	uint32_t length;
	uint16_t flags;
	uint16_t next;
};

struct virtq_used_element {
	uint32_t id;
	uint32_t length;
};

struct virtio_rng_state {
	bool initialized;
	bool available;
	bool request_active;
	bool quarantined;
	bool io_decode_enabled;
	bool bus_master_enabled;
	bool data_device_owned;
	bool descriptor_submitted;
	bool queue_pinned;
	bool command_saved;
	bool initialization_attempted;
	uint16_t io_base;
	uint16_t original_command;
	uint16_t queue_size;
	uint16_t available_index;
	uint16_t used_index;
	struct pci_device_info pci;
	struct dma_buffer queue_buffer;
	struct dma_buffer data_buffer;
	struct dma_buffer_info queue_memory;
	struct dma_buffer_info data_memory;
#ifdef VIRTIO_RNG_TESTING
	struct virtio_rng_test_ops test_ops;
	void *test_context;
#endif
};

static struct virtio_rng_state state;

static uint64_t
irq_save(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
#else
	return 0;
#endif
}

static void
irq_restore(uint64_t flags)
{
#ifdef OPEN_SILLICON_KERNEL
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
#else
	(void)flags;
#endif
}

static bool
pci_read(uint16_t offset, unsigned int width, uint32_t *value)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.pci_read != NULL &&
	    state.test_ops.pci_read(state.test_context, &state.pci, offset,
	    width, value);
#else
	return pci_config_read(state.pci.segment, state.pci.bus,
	    state.pci.device, state.pci.function, offset, width, value);
#endif
}

static bool
pci_write(uint16_t offset, unsigned int width, uint32_t value)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.pci_write != NULL &&
	    state.test_ops.pci_write(state.test_context, &state.pci, offset,
	    width, value);
#else
	return pci_config_write(state.pci.segment, state.pci.bus,
	    state.pci.device, state.pci.function, offset, width, value);
#endif
}

static uint8_t
io_read8(uint16_t offset)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.in8 == NULL ? 0 :
	    state.test_ops.in8(state.test_context,
	    (uint16_t)(state.io_base + offset));
#else
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd"
	    ((uint16_t)(state.io_base + offset)));
	return value;
#endif
}

static uint16_t
io_read16(uint16_t offset)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.in16 == NULL ? 0 :
	    state.test_ops.in16(state.test_context,
	    (uint16_t)(state.io_base + offset));
#else
	uint16_t value;
	__asm__ volatile ("inw %1, %0" : "=a" (value) : "Nd"
	    ((uint16_t)(state.io_base + offset)));
	return value;
#endif
}

static uint32_t
io_read32(uint16_t offset)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.in32 == NULL ? 0 :
	    state.test_ops.in32(state.test_context,
	    (uint16_t)(state.io_base + offset));
#else
	uint32_t value;
	__asm__ volatile ("inl %1, %0" : "=a" (value) : "Nd"
	    ((uint16_t)(state.io_base + offset)));
	return value;
#endif
}

static void
io_write8(uint16_t offset, uint8_t value)
{
#ifdef VIRTIO_RNG_TESTING
	if (state.test_ops.out8 != NULL) {
		state.test_ops.out8(state.test_context,
		    (uint16_t)(state.io_base + offset), value);
		return;
	}
#else
	__asm__ volatile ("outb %0, %1" : : "a" (value), "Nd"
	    ((uint16_t)(state.io_base + offset)));
#endif
}

static void
io_write16(uint16_t offset, uint16_t value)
{
#ifdef VIRTIO_RNG_TESTING
	if (state.test_ops.out16 != NULL) {
		state.test_ops.out16(state.test_context,
		    (uint16_t)(state.io_base + offset), value);
		return;
	}
#else
	__asm__ volatile ("outw %0, %1" : : "a" (value), "Nd"
	    ((uint16_t)(state.io_base + offset)));
#endif
}

static void
io_write32(uint16_t offset, uint32_t value)
{
#ifdef VIRTIO_RNG_TESTING
	if (state.test_ops.out32 != NULL) {
		state.test_ops.out32(state.test_context,
		    (uint16_t)(state.io_base + offset), value);
		return;
	}
#else
	__asm__ volatile ("outl %0, %1" : : "a" (value), "Nd"
	    ((uint16_t)(state.io_base + offset)));
#endif
}

static bool
platform_dma_alloc(size_t size, size_t alignment, unsigned int address_bits,
	struct dma_buffer *buffer, struct dma_buffer_info *info)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.dma_alloc != NULL &&
	    state.test_ops.dma_alloc(state.test_context, size, alignment,
	    address_bits, buffer, info);
#else
	if (!dma_alloc(size, alignment, address_bits, buffer))
		return false;
	if (!dma_buffer_query(*buffer, info)) {
		return false;
	}
	return true;
#endif
}

static bool
dma_acquire(struct dma_buffer buffer)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.dma_acquire != NULL &&
	    state.test_ops.dma_acquire(state.test_context, buffer);
#else
	uint64_t device_address;
	return dma_device_acquire(buffer, &device_address);
#endif
}

static bool
dma_complete(struct dma_buffer buffer)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.dma_complete != NULL &&
	    state.test_ops.dma_complete(state.test_context, buffer);
#else
	return dma_device_complete(buffer);
#endif
}

static bool
dma_pin(struct dma_buffer buffer)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.dma_pin != NULL &&
	    state.test_ops.dma_pin(state.test_context, buffer);
#else
	return dma_descriptor_ring_pin(buffer);
#endif
}

static bool
dma_unpin(struct dma_buffer buffer)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.dma_unpin != NULL &&
	    state.test_ops.dma_unpin(state.test_context, buffer);
#else
	return dma_descriptor_ring_unpin(buffer);
#endif
}

static bool
dma_release(struct dma_buffer buffer)
{
#ifdef VIRTIO_RNG_TESTING
	return state.test_ops.dma_free != NULL &&
	    state.test_ops.dma_free(state.test_context, buffer);
#else
	return dma_free(buffer);
#endif
}

bool
virtio_rng_matches_device(const struct pci_device_info *device)
{
	return device != NULL && device->vendor_id == VIRTIO_RNG_DEVICE_VENDOR &&
	    device->device_id == VIRTIO_RNG_LEGACY_DEVICE;
}

static bool
set_io_decode(bool enable_bus_master)
{
	uint32_t command;
	uint32_t verified;
	uint16_t updated;

	if (!pci_read(PCI_COMMAND_OFFSET, 16, &command))
		return false;
	if (!state.command_saved) {
		state.original_command = (uint16_t)command;
		state.command_saved = true;
	}
	if ((state.original_command & PCI_COMMAND_BUS_MASTER) != 0)
		return false;
	updated = (uint16_t)(state.original_command | PCI_COMMAND_IO);
	if (enable_bus_master)
		updated |= PCI_COMMAND_BUS_MASTER;
	if (!pci_write(PCI_COMMAND_OFFSET, 16, updated))
		return false;
	state.io_decode_enabled = true;
	state.bus_master_enabled = enable_bus_master;
	if (!pci_read(PCI_COMMAND_OFFSET, 16, &verified))
		return false;
	if ((verified & PCI_COMMAND_IO) == 0 ||
	    (((verified & PCI_COMMAND_BUS_MASTER) != 0) != enable_bus_master))
		return false;
	return true;
}

static bool
disable_bus_master(void)
{
	uint32_t command;
	uint32_t verified;

	if (!pci_read(PCI_COMMAND_OFFSET, 16, &command) ||
	    !pci_write(PCI_COMMAND_OFFSET, 16,
	    (uint16_t)(command & ~PCI_COMMAND_BUS_MASTER)) ||
	    !pci_read(PCI_COMMAND_OFFSET, 16, &verified) ||
	    (verified & PCI_COMMAND_BUS_MASTER) != 0)
		return false;
	state.bus_master_enabled = false;
	return true;
}

static uint8_t *
queue_base(void)
{
	return state.queue_memory.cpu_virtual_address;
}

static struct virtq_desc *
queue_descriptors(void)
{
	return (struct virtq_desc *)queue_base();
}

static uint16_t *
queue_available_flags(void)
{
	return (uint16_t *)(queue_base() +
	    (size_t)state.queue_size * sizeof(struct virtq_desc));
}

static uint16_t *
queue_available_index(void)
{
	return queue_available_flags() + 1;
}

static uint16_t *
queue_available_ring(void)
{
	return queue_available_index() + 1;
}

static size_t
queue_used_offset(void)
{
	size_t available_end = (size_t)state.queue_size *
	    sizeof(struct virtq_desc) + 4 +
	    (size_t)state.queue_size * sizeof(uint16_t);
	return (available_end + VIRTQ_ALIGN - 1) & ~(size_t)(VIRTQ_ALIGN - 1);
}

static uint16_t *
queue_used_flags(void)
{
	return (uint16_t *)(queue_base() + queue_used_offset());
}

static uint16_t *
queue_used_index(void)
{
	return queue_used_flags() + 1;
}

static struct virtq_used_element *
queue_used_ring(void)
{
	return (struct virtq_used_element *)(queue_used_index() + 1);
}

static size_t
queue_bytes(uint16_t queue_size)
{
	size_t desc_bytes;
	size_t avail_bytes;
	size_t used_offset;
	size_t used_bytes;

	if (queue_size == 0 || queue_size > VIRTIO_RNG_MAX_QUEUE ||
	    (queue_size & (queue_size - 1)) != 0 ||
	    __builtin_mul_overflow((size_t)queue_size,
	    sizeof(struct virtq_desc), &desc_bytes))
		return 0;
	if (desc_bytes > SIZE_MAX - 4 ||
	    __builtin_mul_overflow((size_t)queue_size, sizeof(uint16_t),
	    &avail_bytes) || avail_bytes > SIZE_MAX - desc_bytes - 4)
		return 0;
	avail_bytes += desc_bytes + 4;
	if (avail_bytes > SIZE_MAX - (VIRTQ_ALIGN - 1))
		return 0;
	used_offset = (avail_bytes + VIRTQ_ALIGN - 1) &
	    ~(size_t)(VIRTQ_ALIGN - 1);
	if (__builtin_mul_overflow((size_t)queue_size,
	    sizeof(struct virtq_used_element), &used_bytes) ||
	    used_bytes > SIZE_MAX - 6)
		return 0;
	used_bytes += 6;
	return used_offset > SIZE_MAX - used_bytes ? 0 : used_offset + used_bytes;
}

static bool
reset_hardware(void)
{
	io_write8(VIRTIO_IO_DEVICE_STATUS, 0);
	for (unsigned int i = 0; i < VIRTIO_RNG_POLL_LIMIT; i++) {
		if (io_read8(VIRTIO_IO_DEVICE_STATUS) == 0)
			return true;
		__asm__ volatile ("pause" : : : "memory");
	}
	return false;
}

static bool
release_dma_after_quiescence(void)
{
	bool success = true;
	if (state.data_device_owned && state.data_buffer.generation != 0 &&
	    !dma_complete(state.data_buffer))
		success = false;
	if (!success)
		return false;
	state.data_device_owned = false;
	if (state.queue_buffer.generation != 0) {
		if (state.queue_pinned && !dma_unpin(state.queue_buffer))
			return false;
		state.queue_pinned = false;
		if (!dma_release(state.queue_buffer))
			return false;
		state.queue_buffer = (struct dma_buffer){0};
	}
	if (state.data_buffer.generation != 0) {
		if (!dma_release(state.data_buffer))
			return false;
		state.data_buffer = (struct dma_buffer){0};
	}
	state.queue_memory = (struct dma_buffer_info){0};
	state.data_memory = (struct dma_buffer_info){0};
	return true;
}

static bool
quiesce_and_release(void)
{
	bool reset = state.io_decode_enabled && reset_hardware();
	bool master_off;

	if (state.io_decode_enabled)
		master_off = disable_bus_master();
	else
		master_off = !state.bus_master_enabled;
	if (!reset || !master_off) {
		state.quarantined = true;
		return false;
	}
	state.descriptor_submitted = false;
	if (!release_dma_after_quiescence()) {
		state.quarantined = true;
		return false;
	}
	if (state.io_decode_enabled) {
		if (!pci_write(PCI_COMMAND_OFFSET, 16, state.original_command)) {
			state.quarantined = true;
			return false;
		}
		state.io_decode_enabled = false;
	}
	return true;
}

static enum virtio_rng_result
initialize(const struct pci_device_info *device)
{
	uint32_t command;
	uint32_t host_features;
	uint16_t queue_size;
	uint32_t queue_pfn;
	size_t bytes;

	if (!virtio_rng_matches_device(device))
		return VIRTIO_RNG_UNAVAILABLE;
	if ((device->bars[0] & 1U) == 0 ||
	    (device->bars[0] & ~0x3U) < 0x1000U ||
	    (device->bars[0] & ~0x3U) > UINT16_MAX - 0x20U)
		return VIRTIO_RNG_UNAVAILABLE;
	state.initialization_attempted = true;
	state.pci = *device;
	state.io_base = (uint16_t)(device->bars[0] & ~0x3U);
	if (!pci_read(PCI_COMMAND_OFFSET, 16, &command) ||
	    (command & PCI_COMMAND_BUS_MASTER) != 0)
		return VIRTIO_RNG_TRANSPORT_FAILURE;
	if (!set_io_decode(false))
		goto failed;
	if (!reset_hardware())
		goto failed;
	io_write8(VIRTIO_IO_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
	io_write8(VIRTIO_IO_DEVICE_STATUS,
	    VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
	host_features = io_read32(VIRTIO_IO_HOST_FEATURES);
	(void)host_features;
	/* The RNG device defines no device-specific features. Negotiate none. */
	io_write32(VIRTIO_IO_GUEST_FEATURES, 0);
	io_write16(VIRTIO_IO_QUEUE_SELECT, 0);
	queue_size = io_read16(VIRTIO_IO_QUEUE_SIZE);
	bytes = queue_bytes(queue_size);
	if (bytes == 0)
		goto failed;
	if (!platform_dma_alloc(bytes, VIRTQ_ALIGN, 32, &state.queue_buffer,
	    &state.queue_memory))
		goto failed;
	if ((state.queue_memory.physical_address & (VIRTQ_ALIGN - 1)) != 0 ||
	    state.queue_memory.device_address != state.queue_memory.physical_address ||
	    state.queue_memory.physical_address > UINT32_MAX ||
	    state.queue_memory.allocated_size < bytes ||
	    state.queue_memory.allocated_size - 1 >
	    UINT32_MAX - state.queue_memory.physical_address ||
	    state.queue_memory.cpu_virtual_address == NULL ||
	    !dma_pin(state.queue_buffer))
		goto failed;
	state.queue_pinned = true;
	if (!platform_dma_alloc(VIRTIO_RNG_DATA_BYTES, 16, 32,
	    &state.data_buffer,
	    &state.data_memory) ||
	    state.data_memory.device_address != state.data_memory.physical_address ||
	    state.data_memory.requested_size < VIRTIO_RNG_DATA_BYTES ||
	    state.data_memory.allocated_size == 0 ||
	    state.data_memory.cpu_virtual_address == NULL)
		goto failed;
	if (state.data_memory.physical_address > UINT32_MAX ||
	    state.data_memory.allocated_size - 1 >
	    UINT32_MAX - state.data_memory.physical_address ||
	    state.data_memory.requested_size > UINT32_MAX)
		goto failed;
	queue_pfn = (uint32_t)(state.queue_memory.physical_address / VIRTQ_ALIGN);
	io_write32(VIRTIO_IO_QUEUE_PFN, queue_pfn);
	if (io_read32(VIRTIO_IO_QUEUE_PFN) != queue_pfn ||
	    !set_io_decode(true))
		goto failed;
	io_write8(VIRTIO_IO_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
	    VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
	if ((io_read8(VIRTIO_IO_DEVICE_STATUS) &
	    (VIRTIO_STATUS_DRIVER_OK | VIRTIO_STATUS_FAILED)) !=
	    VIRTIO_STATUS_DRIVER_OK)
		goto failed;
	state.queue_size = queue_size;
	state.available_index = 0;
	state.used_index = 0;
	state.initialized = true;
	__atomic_store_n(&state.available, true, __ATOMIC_RELEASE);
	return VIRTIO_RNG_OK;

failed:
	__atomic_store_n(&state.available, false, __ATOMIC_RELEASE);
	if (state.io_decode_enabled && !quiesce_and_release())
		return VIRTIO_RNG_TRANSPORT_FAILURE;
	state.initialized = false;
	return VIRTIO_RNG_TRANSPORT_FAILURE;
}

enum virtio_rng_result
virtio_rng_init(const struct pci_device_info *device)
{
	if (state.initialization_attempted || state.initialized ||
	    state.quarantined)
		return VIRTIO_RNG_TRANSPORT_FAILURE;
	return initialize(device);
}

bool
virtio_rng_available(void)
{
	return __atomic_load_n(&state.available, __ATOMIC_ACQUIRE);
}

enum virtio_rng_result
virtio_rng_read(void *output, size_t capacity, size_t *received)
{
	uint64_t flags;
	uint16_t observed;
	struct virtq_used_element completion;
	uint8_t *data;
	uint16_t *available_ring;

	if (received == NULL)
		return VIRTIO_RNG_INVALID_ARGUMENT;
	*received = 0;
	if (capacity > VIRTIO_RNG_MAX_READ ||
	    (capacity != 0 && output == NULL))
		return VIRTIO_RNG_INVALID_ARGUMENT;
	if (!virtio_rng_available())
		return VIRTIO_RNG_UNAVAILABLE;
	if (capacity == 0)
		return VIRTIO_RNG_OK;
	flags = irq_save();
	if (__atomic_exchange_n(&state.request_active, true, __ATOMIC_ACQUIRE)) {
		irq_restore(flags);
		return VIRTIO_RNG_BUSY;
	}
	irq_restore(flags);
	/* Shutdown can race after the initial availability check on another CPU. */
	if (!virtio_rng_available()) {
		__atomic_store_n(&state.request_active, false, __ATOMIC_RELEASE);
		return VIRTIO_RNG_UNAVAILABLE;
	}

	data = state.data_memory.cpu_virtual_address;
	observed = state.used_index;
	available_ring = queue_available_ring();
	for (size_t i = 0; i < capacity; i++)
		data[i] = 0;
	queue_descriptors()[0] = (struct virtq_desc){
		.address = state.data_memory.device_address,
		.length = (uint32_t)capacity,
		.flags = VIRTQ_DESC_F_WRITE,
		.next = 0
	};
	available_ring[state.available_index % state.queue_size] = 0;
	if (!dma_acquire(state.data_buffer)) {
		flags = irq_save();
		__atomic_store_n(&state.request_active, false, __ATOMIC_RELEASE);
		irq_restore(flags);
		return VIRTIO_RNG_TRANSPORT_FAILURE;
	}
	state.data_device_owned = true;
	__asm__ volatile ("mfence" : : : "memory");
	state.available_index++;
	*queue_available_index() = state.available_index;
	state.descriptor_submitted = true;
	__asm__ volatile ("mfence" : : : "memory");
	io_write16(VIRTIO_IO_QUEUE_NOTIFY, 0);

	for (unsigned int poll = 0; poll < VIRTIO_RNG_POLL_LIMIT; poll++) {
		__asm__ volatile ("mfence" : : : "memory");
		observed = *queue_used_index();
		if (observed != state.used_index)
			break;
		__asm__ volatile ("pause" : : : "memory");
	}
	if (observed == state.used_index) {
		__atomic_store_n(&state.available, false, __ATOMIC_RELEASE);
		{
			bool stopped = quiesce_and_release();
			flags = irq_save();
			__atomic_store_n(&state.request_active, false, __ATOMIC_RELEASE);
			irq_restore(flags);
			if (stopped)
				state.initialized = false;
			return stopped ? VIRTIO_RNG_TIMEOUT :
			    VIRTIO_RNG_TRANSPORT_FAILURE;
		}
	}
	__asm__ volatile ("mfence" : : : "memory");
	completion = queue_used_ring()[state.used_index % state.queue_size];
	if ((uint16_t)(observed - state.used_index) != 1 || completion.id != 0 ||
	    completion.length == 0 || completion.length > capacity) {
		__atomic_store_n(&state.available, false, __ATOMIC_RELEASE);
		{
			bool stopped = quiesce_and_release();
			flags = irq_save();
			__atomic_store_n(&state.request_active, false, __ATOMIC_RELEASE);
			irq_restore(flags);
			if (stopped)
				state.initialized = false;
			return stopped ? VIRTIO_RNG_MALFORMED_COMPLETION :
			    VIRTIO_RNG_TRANSPORT_FAILURE;
		}
	}
	if (!dma_complete(state.data_buffer)) {
		__atomic_store_n(&state.available, false, __ATOMIC_RELEASE);
		(void)quiesce_and_release();
		flags = irq_save();
		__atomic_store_n(&state.request_active, false, __ATOMIC_RELEASE);
		irq_restore(flags);
		state.initialized = false;
		return VIRTIO_RNG_TRANSPORT_FAILURE;
	}
	state.data_device_owned = false;
	state.descriptor_submitted = false;
	state.used_index = observed;
	for (uint32_t i = 0; i < completion.length; i++)
		((uint8_t *)output)[i] = data[i];
	*received = completion.length;
	flags = irq_save();
	__atomic_store_n(&state.request_active, false, __ATOMIC_RELEASE);
	irq_restore(flags);
	return VIRTIO_RNG_OK;
}

enum virtio_rng_result
virtio_rng_shutdown(void)
{
	uint64_t flags = irq_save();
	__atomic_store_n(&state.available, false, __ATOMIC_RELEASE);
	if (__atomic_load_n(&state.request_active, __ATOMIC_ACQUIRE)) {
		irq_restore(flags);
		return VIRTIO_RNG_BUSY;
	}
	irq_restore(flags);
	if (!state.initialized && state.queue_buffer.generation == 0 &&
	    state.data_buffer.generation == 0)
		return VIRTIO_RNG_OK;
	if (!quiesce_and_release())
		return VIRTIO_RNG_TRANSPORT_FAILURE;
	state.initialized = false;
	return VIRTIO_RNG_OK;
}

#ifdef VIRTIO_RNG_TESTING
bool
virtio_rng_test_init(const struct pci_device_info *device,
	const struct virtio_rng_test_ops *ops, void *context)
{
	if (ops == NULL || ops->pci_read == NULL || ops->pci_write == NULL ||
	    ops->in8 == NULL || ops->in16 == NULL || ops->in32 == NULL ||
	    ops->out8 == NULL || ops->out16 == NULL || ops->out32 == NULL ||
	    ops->dma_alloc == NULL || ops->dma_acquire == NULL ||
	    ops->dma_complete == NULL || ops->dma_pin == NULL ||
	    ops->dma_unpin == NULL || ops->dma_free == NULL || state.initialized)
		return false;
	state.test_ops = *ops;
	state.test_context = context;
	return initialize(device) == VIRTIO_RNG_OK;
}

void
virtio_rng_test_reset_state(void)
{
	state = (struct virtio_rng_state){0};
}
#endif
