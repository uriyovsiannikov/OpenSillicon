#include <stddef.h>
#include <stdint.h>

#include "kernel/pmm.h"

#define PAGE_SIZE 4096ULL

struct pmm_state {
	uint8_t *used;
	uint64_t frame_count;
	uint64_t free_count;
	uint64_t search_hint;
	uint64_t metadata_start;
	uint64_t metadata_end;
	const struct limine_memmap_response *memmap;
	bool initialized;
};

static struct pmm_state pmm;

static bool
range_end(uint64_t base, uint64_t length, uint64_t *end)
{
	if (length == 0 || base > UINT64_MAX - length)
		return false;
	*end = base + length;
	return true;
}

static bool
bit_is_set(const uint8_t *bitmap, uint64_t index)
{
	return (bitmap[index / 8] & (uint8_t)(1U << (index % 8))) != 0;
}

static void
bit_set(uint8_t *bitmap, uint64_t index)
{
	bitmap[index / 8] |= (uint8_t)(1U << (index % 8));
}

static void
bit_clear(uint8_t *bitmap, uint64_t index)
{
	bitmap[index / 8] &= (uint8_t)~(1U << (index % 8));
}

static bool
bitmap_phys_range(void *bitmap, size_t size, uint64_t hhdm_offset,
	uint64_t *start, uint64_t *end)
{
	uintptr_t virtual_start = (uintptr_t)bitmap;
	uint64_t physical_start;

	if (virtual_start < hhdm_offset)
		return false;
	physical_start = virtual_start - hhdm_offset;
	if (size == 0 || physical_start > UINT64_MAX - size)
		return false;
	*start = physical_start;
	*end = physical_start + size;
	return true;
}

static void
reserve_metadata_pages(uint64_t start, uint64_t end)
{
	uint64_t first = start / PAGE_SIZE;
	uint64_t last = end / PAGE_SIZE + (end % PAGE_SIZE != 0);
	uint64_t frame;

	pmm.metadata_start = first * PAGE_SIZE;
	pmm.metadata_end = last * PAGE_SIZE;
	for (frame = first; frame < last && frame < pmm.frame_count; frame++) {
		if (!bit_is_set(pmm.used, frame))
			pmm.free_count--;
		bit_set(pmm.used, frame);
	}
}

static bool
page_is_usable(uint64_t physical_address)
{
	uint64_t page_end;
	uint64_t i;

	if (!range_end(physical_address, PAGE_SIZE, &page_end))
		return false;
	for (i = 0; i < pmm.memmap->entry_count; i++) {
		const struct limine_memmap_entry *entry = pmm.memmap->entries[i];
		uint64_t end;

		if (entry->type != LIMINE_MEMMAP_USABLE ||
		    !range_end(entry->base, entry->length, &end))
			continue;
		if (physical_address >= entry->base && page_end <= end)
			return true;
	}
	return false;
}

bool
pmm_init(const struct boot_info *info, struct bootstrap_allocator *bootstrap)
{
	const struct limine_memmap_response *memmap;
	uint64_t max_address = 0;
	uint64_t frame_count;
	uint64_t bitmap_bytes_u64;
	size_t bitmap_bytes;
	uint64_t i;
	uint64_t frame;
	uint64_t first;
	uint64_t end_frame;
	uint64_t start_a;
	uint64_t end_a;

	pmm = (struct pmm_state){0};
	if (info == NULL || info->memmap == NULL || bootstrap == NULL ||
	    bootstrap->memmap != info->memmap)
		return false;
	memmap = info->memmap;

	for (i = 0; i < memmap->entry_count; i++) {
		uint64_t end;

		if (!range_end(memmap->entries[i]->base, memmap->entries[i]->length,
		    &end))
			return false;
		if (memmap->entries[i]->type == LIMINE_MEMMAP_USABLE &&
		    end > max_address)
			max_address = end;
	}
	frame_count = max_address / PAGE_SIZE +
	    (max_address % PAGE_SIZE != 0);
	if (frame_count == 0 || frame_count > UINT64_MAX - 7)
		return false;
	bitmap_bytes_u64 = (frame_count + 7) / 8;
	if (bitmap_bytes_u64 > SIZE_MAX)
		return false;
	bitmap_bytes = (size_t)bitmap_bytes_u64;

	pmm.frame_count = frame_count;
	pmm.memmap = memmap;
	pmm.used = bootstrap_alloc(bootstrap, bitmap_bytes, 8);
	if (pmm.used == NULL) {
		pmm = (struct pmm_state){0};
		return false;
	}
	for (i = 0; i < bitmap_bytes; i++)
		pmm.used[i] = UINT8_MAX;

	for (i = 0; i < memmap->entry_count; i++) {
		const struct limine_memmap_entry *entry = memmap->entries[i];
		uint64_t end;

		if (entry->type != LIMINE_MEMMAP_USABLE)
			continue;
		if (!range_end(entry->base, entry->length, &end))
			return false;
		first = entry->base / PAGE_SIZE + (entry->base % PAGE_SIZE != 0);
		end_frame = end / PAGE_SIZE;
		for (frame = first; frame < end_frame && frame < pmm.frame_count;
		    frame++) {
			if (bit_is_set(pmm.used, frame)) {
				bit_clear(pmm.used, frame);
				pmm.free_count++;
			}
		}
	}

	if (!bitmap_phys_range(pmm.used, bitmap_bytes, info->hhdm_offset,
	    &start_a, &end_a)) {
		pmm = (struct pmm_state){0};
		return false;
	}
	reserve_metadata_pages(start_a, end_a);
	pmm.initialized = true;
	return true;
}

bool
pmm_alloc_page(uint64_t *physical_address)
{
	uint64_t checked;
	uint64_t frame;

	if (!pmm.initialized || physical_address == NULL || pmm.free_count == 0)
		return false;
	for (checked = 0; checked < pmm.frame_count; checked++) {
		frame = pmm.search_hint + checked;
		if (frame >= pmm.frame_count)
			frame -= pmm.frame_count;
		if (bit_is_set(pmm.used, frame))
			continue;
		bit_set(pmm.used, frame);
		pmm.free_count--;
		pmm.search_hint = frame + 1;
		if (pmm.search_hint == pmm.frame_count)
			pmm.search_hint = 0;
		*physical_address = frame * PAGE_SIZE;
		return true;
	}
	return false;
}

bool
pmm_free_page(uint64_t physical_address)
{
	uint64_t frame;

	if (!pmm.initialized || (physical_address & (PAGE_SIZE - 1)) != 0 ||
	    (physical_address >= pmm.metadata_start &&
	    physical_address < pmm.metadata_end) || !page_is_usable(physical_address))
		return false;
	frame = physical_address / PAGE_SIZE;
	if (frame >= pmm.frame_count || !bit_is_set(pmm.used, frame))
		return false;
	bit_clear(pmm.used, frame);
	pmm.free_count++;
	if (frame < pmm.search_hint)
		pmm.search_hint = frame;
	return true;
}

uint64_t
pmm_total_pages(void)
{
	return pmm.initialized ? pmm.frame_count : 0;
}

uint64_t
pmm_free_pages(void)
{
	return pmm.initialized ? pmm.free_count : 0;
}
