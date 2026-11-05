#include <stdint.h>
#include <stddef.h>

#include "kernel/heap.h"
#include "kernel/pmm.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096ULL
#define HEAP_MAGIC 0x4f5348454150424cULL
#define BLOCK_ALLOCATED 1ULL
#define BLOCK_MIN_SIZE 32ULL

struct heap_block {
	uint64_t size;
	uint64_t state;
};

static uint64_t heap_start;
static uint64_t heap_end;
static bool initialized;
static struct spinlock heap_lock = SPINLOCK_INITIALIZER;

static uint64_t *
footer(struct heap_block *block)
{
	return (uint64_t *)((uintptr_t)block + block->size - sizeof(uint64_t));
}

static void
set_block(struct heap_block *block, uint64_t size, uint64_t state)
{
	block->size = size;
	block->state = state;
	*footer(block) = size;
}

static struct heap_block *
next_block(struct heap_block *block)
{
	return (struct heap_block *)((uintptr_t)block + block->size);
}

static struct heap_block *
last_block(void)
{
	struct heap_block *block = (struct heap_block *)(uintptr_t)heap_start;
	uint64_t block_address;
	uint64_t next_address;

	if (heap_start == heap_end)
		return NULL;
	for (;;) {
		block_address = (uint64_t)(uintptr_t)block;
		if (block->size < BLOCK_MIN_SIZE || block->size > heap_end - block_address)
			return NULL;
		next_address = block_address + block->size;
		if (next_address == heap_end)
			return block;
		block = next_block(block);
	}
}

static bool
grow_heap(uint64_t minimum)
{
	uint64_t growth = (minimum + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	uint64_t old_end = heap_end;
	uint64_t mapped = 0;
	struct heap_block *previous = last_block();

	/* vmm_map_page serializes shared kernel page-table updates and performs a
	 * synchronous shootdown on every other online CPU. */
	if (minimum > UINT64_MAX - (PAGE_SIZE - 1) || growth < minimum ||
	    heap_end > vmm_heap_end() ||
	    growth > vmm_heap_end() - heap_end ||
	    (old_end != heap_start && previous == NULL))
		return false;
	while (mapped < growth) {
		uint64_t physical;

		if (!pmm_alloc_page(&physical))
			break;
		if (!vmm_map_page(vmm_kernel_address_space(), old_end + mapped,
		    physical, VMM_WRITE)) {
			(void)pmm_free_page(physical);
			break;
		}
		mapped += PAGE_SIZE;
	}
	if (mapped != growth) {
		while (mapped != 0) {
			uint64_t physical;

			mapped -= PAGE_SIZE;
			if (vmm_unmap_page(vmm_kernel_address_space(), old_end + mapped,
			    &physical))
				(void)pmm_free_page(physical);
		}
		return false;
	}
	heap_end += growth;
	if (previous != NULL && previous->state == 0) {
		set_block(previous, previous->size + growth, 0);
	} else {
		set_block((struct heap_block *)(uintptr_t)old_end, growth, 0);
	}
	return true;
}

static bool
kheap_init_locked(void)
{
	if (initialized || vmm_kernel_address_space() == NULL ||
	    vmm_heap_start() >= vmm_heap_end())
		return false;
	heap_start = vmm_heap_start();
	heap_end = heap_start;
	initialized = true;
	return true;
}

bool
kheap_init(void)
{
	uint64_t flags = spinlock_lock_irqsave(&heap_lock);
	bool result = kheap_init_locked();

	spinlock_unlock_irqrestore(&heap_lock, flags);
	return result;
}

static void *
kheap_alloc_locked(size_t size)
{
	uint64_t needed;
	struct heap_block *block;

	if (!initialized || size == 0 || size > UINT64_MAX -
	    (sizeof(struct heap_block) + sizeof(uint64_t) + 15ULL))
		return NULL;
	needed = ((uint64_t)size + sizeof(struct heap_block) + sizeof(uint64_t) + 15)
	    & ~15ULL;
	if (needed < BLOCK_MIN_SIZE)
		needed = BLOCK_MIN_SIZE;
	for (;;) {
		block = (struct heap_block *)(uintptr_t)heap_start;
		while ((uint64_t)(uintptr_t)block < heap_end) {
			if (block->state == 0 && block->size >= needed) {
				uint64_t remainder = block->size - needed;
				uint64_t used = block->size;

				if (remainder >= BLOCK_MIN_SIZE) {
					used = needed;
					set_block((struct heap_block *)((uintptr_t)block + used),
					    remainder, 0);
				}
				set_block(block, used, HEAP_MAGIC | BLOCK_ALLOCATED);
				return (void *)((uintptr_t)block + sizeof(*block));
			}
			if (block->size < BLOCK_MIN_SIZE || block->size > heap_end -
			    (uint64_t)(uintptr_t)block)
				return NULL;
			block = next_block(block);
		}
		if (!grow_heap(needed))
			return NULL;
	}
}

void *
kheap_alloc(size_t size)
{
	uint64_t flags = spinlock_lock_irqsave(&heap_lock);
	void *result = kheap_alloc_locked(size);

	spinlock_unlock_irqrestore(&heap_lock, flags);
	return result;
}

static bool
kheap_free_locked(void *allocation)
{
	struct heap_block *block;
	uint64_t block_address;

	if (!initialized || allocation == NULL)
		return false;
	block_address = (uintptr_t)allocation - sizeof(*block);
	if ((uintptr_t)allocation < sizeof(*block) || block_address < heap_start ||
	    block_address >= heap_end ||
	    (block_address & (sizeof(uint64_t) - 1)) != 0)
		return false;
	block = (struct heap_block *)(uintptr_t)block_address;
	if (block->state != (HEAP_MAGIC | BLOCK_ALLOCATED) ||
	    block->size < BLOCK_MIN_SIZE || block->size > heap_end - block_address ||
	    *footer(block) != block->size)
		return false;
	set_block(block, block->size, 0);
	if (block_address + block->size < heap_end) {
		struct heap_block *next = next_block(block);

		if (next->state == 0)
			set_block(block, block->size + next->size, 0);
	}
	if (block_address > heap_start) {
		uint64_t previous_size = *(uint64_t *)(uintptr_t)(block_address -
		    sizeof(uint64_t));

		if (previous_size >= BLOCK_MIN_SIZE && previous_size <= block_address -
		    heap_start) {
			struct heap_block *previous = (struct heap_block *)(uintptr_t)
			    (block_address - previous_size);

			if (previous->state == 0 && previous->size == previous_size)
				set_block(previous, previous->size + block->size, 0);
		}
	}
	return true;
}

bool
kheap_free(void *allocation)
{
	uint64_t flags = spinlock_lock_irqsave(&heap_lock);
	bool result = kheap_free_locked(allocation);

	spinlock_unlock_irqrestore(&heap_lock, flags);
	return result;
}
