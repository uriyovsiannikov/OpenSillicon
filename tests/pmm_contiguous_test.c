#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/pmm.h"

#define TEST_BASE 0x100000ULL
#define TEST_LENGTH (32ULL * 1024ULL * 1024ULL)

static _Alignas(4096) uint8_t bitmap[4096];
static struct limine_memmap_entry usable;
static struct limine_memmap_entry *entries[1];
static struct limine_memmap_response map;

void *
bootstrap_alloc(struct bootstrap_allocator *allocator, size_t size,
	size_t alignment)
{
	(void)allocator;
	(void)alignment;
	assert(size <= sizeof(bitmap));
	return bitmap;
}

static void
test_contiguous_allocation(void)
{
	struct boot_info info = {0};
	struct bootstrap_allocator bootstrap = {0};
	uint64_t address;
	uint64_t before;

	usable = (struct limine_memmap_entry){
		.base = TEST_BASE,
		.length = TEST_LENGTH,
		.type = LIMINE_MEMMAP_USABLE
	};
	entries[0] = &usable;
	map = (struct limine_memmap_response){
		.entry_count = 1,
		.entries = entries
	};
	info.memmap = &map;
	info.hhdm_offset = (uintptr_t)bitmap - TEST_BASE;
	bootstrap.memmap = &map;
	bootstrap.hhdm_offset = info.hhdm_offset;
	assert(pmm_init(&info, &bootstrap));
	before = pmm_free_pages();
	assert(!pmm_alloc_contiguous(0, 1, TEST_BASE + TEST_LENGTH, &address));
	assert(!pmm_alloc_contiguous(2, 3, TEST_BASE + TEST_LENGTH, &address));
	assert(!pmm_alloc_contiguous(2, 1, 0x101000, &address));
	assert(pmm_alloc_contiguous(3, 4, TEST_BASE + TEST_LENGTH, &address));
	assert((address & (4 * 4096 - 1)) == 0);
	assert(address >= TEST_BASE && address + 3 * 4096 <= TEST_BASE + TEST_LENGTH);
	assert(pmm_free_pages() == before - 3);
	assert(pmm_free_contiguous(address, 3));
	assert(pmm_free_pages() == before);
	assert(!pmm_free_contiguous(address, 3));
	assert(pmm_alloc_contiguous(1, 1, TEST_BASE + 0x102000, &address));
	assert(address + 4096 <= TEST_BASE + 0x102000);
	assert(pmm_free_contiguous(address, 1));
}

int
main(void)
{
	test_contiguous_allocation();
	puts("PMM contiguous allocation and address-bound tests passed");
	return 0;
}
