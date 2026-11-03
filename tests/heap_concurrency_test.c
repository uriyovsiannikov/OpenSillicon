#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/heap.h"
#include "kernel/pmm.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096U
#define HEAP_SIZE (8U * 1024U * 1024U)
#define THREAD_COUNT 4U
#define ALLOCATIONS_PER_THREAD 512U

struct test_allocation {
	void *address;
	size_t size;
	uint64_t marker;
};

static uint8_t *heap_memory;
static uint64_t next_physical = 0x100000;
static struct test_allocation allocations[THREAD_COUNT][ALLOCATIONS_PER_THREAD];
static atomic_bool failed;

struct address_space *
vmm_kernel_address_space(void)
{
	return (struct address_space *)(uintptr_t)1;
}

uint64_t
vmm_heap_start(void)
{
	return (uintptr_t)heap_memory;
}

uint64_t
vmm_heap_end(void)
{
	return (uintptr_t)heap_memory + HEAP_SIZE;
}

bool
vmm_map_page(struct address_space *space, uint64_t virtual_address,
	uint64_t physical_address, unsigned int flags)
{
	(void)space;
	return virtual_address >= vmm_heap_start() &&
	    virtual_address + PAGE_SIZE <= vmm_heap_end() &&
	    (physical_address & (PAGE_SIZE - 1)) == 0 && flags == VMM_WRITE;
}

bool
vmm_unmap_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address)
{
	(void)space;
	if (virtual_address < vmm_heap_start() ||
	    virtual_address + PAGE_SIZE > vmm_heap_end() || physical_address == NULL)
		return false;
	*physical_address = 0;
	return true;
}

bool
pmm_alloc_page(uint64_t *physical_address)
{
	if (physical_address == NULL || next_physical > UINT64_MAX - PAGE_SIZE)
		return false;
	*physical_address = next_physical;
	next_physical += PAGE_SIZE;
	return true;
}

bool
pmm_free_page(uint64_t physical_address)
{
	return (physical_address & (PAGE_SIZE - 1)) == 0;
}

static void *
allocate_worker(void *argument)
{
	size_t thread = (size_t)(uintptr_t)argument;
	for (size_t i = 0; i < ALLOCATIONS_PER_THREAD; i++) {
		struct test_allocation *allocation = &allocations[thread][i];
		allocation->size = 64U + ((i * 131U + thread * 97U) % 1984U);
		allocation->marker = ((uint64_t)thread << 32) | (i + 1U);
		allocation->address = kheap_alloc(allocation->size);
		if (allocation->address == NULL) {
			atomic_store(&failed, true);
			continue;
		}
		*(uint64_t *)allocation->address = allocation->marker;
	}
	return NULL;
}

static void *
free_worker(void *argument)
{
	size_t thread = (size_t)(uintptr_t)argument;
	for (size_t i = 0; i < ALLOCATIONS_PER_THREAD; i++) {
		struct test_allocation allocation = allocations[thread][i];
		if (allocation.address == NULL)
			continue;
		if (*(uint64_t *)allocation.address != allocation.marker ||
		    !kheap_free(allocation.address) || kheap_free(allocation.address))
			atomic_store(&failed, true);
	}
	return NULL;
}

static bool
run_workers(void *(*worker)(void *))
{
	pthread_t threads[THREAD_COUNT];
	for (size_t i = 0; i < THREAD_COUNT; i++)
		if (pthread_create(&threads[i], NULL, worker,
		    (void *)(uintptr_t)i) != 0)
			return false;
	for (size_t i = 0; i < THREAD_COUNT; i++)
		if (pthread_join(threads[i], NULL) != 0)
			return false;
	return true;
}

int
main(void)
{
	void *large;
	if (posix_memalign((void **)&heap_memory, PAGE_SIZE, HEAP_SIZE) != 0)
		return 1;
	if (!kheap_init() || !run_workers(allocate_worker) || atomic_load(&failed)) {
		fprintf(stderr, "concurrent heap allocation failed\n");
		return 1;
	}
	if (!run_workers(free_worker) || atomic_load(&failed)) {
		fprintf(stderr, "concurrent heap free or coalescing failed\n");
		return 1;
	}
	large = kheap_alloc(HEAP_SIZE / 2);
	if (large == NULL || !kheap_free(large)) {
		fprintf(stderr, "heap did not recover after concurrent frees\n");
		return 1;
	}
	free(heap_memory);
	puts("heap concurrent allocation test passed");
	return 0;
}
