#define _POSIX_C_SOURCE 200112L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/pmm.h"

#define PAGE_SIZE 4096U
#define THREAD_COUNT 4U
#define ALLOCATIONS_PER_THREAD 512U
#define PHYSICAL_BASE (1ULL << 20)
#define REGION_SIZE (64ULL << 20)
#define REGION_END (PHYSICAL_BASE + REGION_SIZE)

struct allocation {
	uint64_t address;
	size_t pages;
};

static uint8_t *test_memory;
static size_t bootstrap_offset;
static struct allocation allocations[THREAD_COUNT][ALLOCATIONS_PER_THREAD];
static atomic_bool failed;

void *
bootstrap_alloc(struct bootstrap_allocator *allocator, size_t size,
	size_t alignment)
{
	size_t aligned;
	(void)allocator;
	if (alignment == 0 || (alignment & (alignment - 1)) != 0 ||
	    bootstrap_offset > SIZE_MAX - (alignment - 1))
		return NULL;
	aligned = (bootstrap_offset + alignment - 1) & ~(alignment - 1);
	if (aligned > REGION_SIZE || size > REGION_SIZE - aligned)
		return NULL;
	bootstrap_offset = aligned + size;
	return test_memory + aligned;
}

static void *
allocate_worker(void *argument)
{
	size_t thread = (size_t)(uintptr_t)argument;
	for (size_t i = 0; i < ALLOCATIONS_PER_THREAD; i++) {
		struct allocation *allocation = &allocations[thread][i];

		allocation->pages = i % 2 == 0 ? 1 : 2;
		if (allocation->pages == 1) {
			if (!pmm_alloc_page(&allocation->address))
				atomic_store(&failed, true);
		} else if (!pmm_alloc_contiguous(2, 2, REGION_END,
		    &allocation->address)) {
			atomic_store(&failed, true);
		}
	}
	return NULL;
}

static void *
free_worker(void *argument)
{
	size_t thread = (size_t)(uintptr_t)argument;
	for (size_t i = 0; i < ALLOCATIONS_PER_THREAD; i++) {
		struct allocation allocation = allocations[thread][i];
		bool result = allocation.pages == 1 ?
		    pmm_free_page(allocation.address) :
		    pmm_free_contiguous(allocation.address, allocation.pages);
		if (!result)
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
	struct limine_memmap_entry entry = {
		.base = PHYSICAL_BASE,
		.length = REGION_SIZE,
		.type = LIMINE_MEMMAP_USABLE
	};
	struct limine_memmap_entry *entries[] = { &entry };
	struct limine_memmap_response memmap = {
		.entry_count = 1,
		.entries = entries
	};
	struct boot_info info = {
		.memmap = &memmap
	};
	struct bootstrap_allocator bootstrap = {
		.memmap = &memmap
	};
	uint64_t free_before;
	bool allocated_pages[REGION_END / PAGE_SIZE] = { false };

	if (posix_memalign((void **)&test_memory, PAGE_SIZE, REGION_SIZE) != 0)
		return 1;
	info.hhdm_offset = (uintptr_t)test_memory - PHYSICAL_BASE;
	bootstrap.hhdm_offset = info.hhdm_offset;
	if (!pmm_init(&info, &bootstrap)) {
		fprintf(stderr, "PMM fixture initialization failed\n");
		return 1;
	}
	free_before = pmm_free_pages();
	if (!run_workers(allocate_worker) || atomic_load(&failed)) {
		fprintf(stderr, "concurrent PMM allocation failed\n");
		return 1;
	}
	for (size_t thread = 0; thread < THREAD_COUNT; thread++) {
		for (size_t i = 0; i < ALLOCATIONS_PER_THREAD; i++) {
			struct allocation allocation = allocations[thread][i];
			uint64_t first = allocation.address / PAGE_SIZE;
			if (first < PHYSICAL_BASE / PAGE_SIZE ||
			    first >= REGION_END / PAGE_SIZE ||
			    allocation.pages > REGION_END / PAGE_SIZE - first) {
				fprintf(stderr, "PMM returned an out-of-range allocation\n");
				return 1;
			}
			for (size_t page = 0; page < allocation.pages; page++) {
				if (allocated_pages[first + page]) {
					fprintf(stderr, "PMM returned overlapping allocations\n");
					return 1;
				}
				allocated_pages[first + page] = true;
			}
		}
	}
	if (!run_workers(free_worker) || atomic_load(&failed) ||
	    pmm_free_pages() != free_before) {
		fprintf(stderr, "concurrent PMM frees did not restore free count\n");
		return 1;
	}
	free(test_memory);
	puts("PMM concurrent allocation test passed");
	return 0;
}
