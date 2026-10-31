#include "kernel/boot.h"

#define PAGE_SIZE 4096U

static bool
is_power_of_two(size_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static bool
range_end(uint64_t base, uint64_t length, uint64_t *end)
{
	if (length == 0 || base > UINT64_MAX - length)
		return false;
	*end = base + length;
	return true;
}

static bool
memmap_valid(const struct limine_memmap_response *memmap)
{
	uint64_t i;
	uint64_t j;
	uintptr_t entries_address;

	if (memmap == NULL || memmap->entry_count == 0 || memmap->entries == NULL)
		return false;
	if (memmap->entry_count > SIZE_MAX / sizeof(*memmap->entries))
		return false;
	entries_address = (uintptr_t)memmap->entries;
	if (memmap->entry_count > (UINTPTR_MAX - entries_address) /
	    sizeof(*memmap->entries))
		return false;

	for (i = 0; i < memmap->entry_count; i++) {
		const struct limine_memmap_entry *entry = memmap->entries[i];
		uint64_t end;

		if (entry == NULL || !range_end(entry->base, entry->length, &end))
			return false;
		for (j = 0; j < i; j++) {
			const struct limine_memmap_entry *previous = memmap->entries[j];
			uint64_t previous_end;

			if (!range_end(previous->base, previous->length, &previous_end))
				return false;
			if (entry->base < previous_end && previous->base < end)
				return false;
		}
	}
	return true;
}

static bool
framebuffer_valid(const struct limine_framebuffer *framebuffer)
{
	uint64_t row_bits;
	uint64_t minimum_pitch;
	uint64_t framebuffer_size;

	if (framebuffer == NULL || framebuffer->address == NULL ||
	    framebuffer->width == 0 || framebuffer->height == 0 ||
	    framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
	    framebuffer->bpp < 15 || framebuffer->bpp > 32)
		return false;
	if (framebuffer->width > (UINT64_MAX - 7) / framebuffer->bpp)
		return false;
	row_bits = framebuffer->width * framebuffer->bpp;
	minimum_pitch = (row_bits + 7) / 8;
	if (framebuffer->pitch < minimum_pitch ||
	    framebuffer->height > UINT64_MAX / framebuffer->pitch)
		return false;
	framebuffer_size = framebuffer->pitch * framebuffer->height;
	return framebuffer_size - 1 <= UINTPTR_MAX -
	    (uintptr_t)framebuffer->address;
}

enum boot_info_error
boot_info_parse(const struct limine_firmware_type_response *firmware,
	const struct limine_hhdm_response *hhdm,
	const struct limine_memmap_response *memmap,
	const struct limine_framebuffer_response *framebuffers,
	struct boot_info *info)
{
	const struct limine_framebuffer *framebuffer = NULL;

	if (info == NULL)
		return BOOT_INFO_MISSING_RESPONSE;
	*info = (struct boot_info){0};
	if (firmware == NULL || hhdm == NULL || memmap == NULL)
		return BOOT_INFO_MISSING_RESPONSE;
	if (firmware->firmware_type != LIMINE_FIRMWARE_TYPE_EFI64)
		return BOOT_INFO_INVALID_FIRMWARE;
	if (hhdm->offset == 0 || (hhdm->offset & (PAGE_SIZE - 1)) != 0)
		return BOOT_INFO_INVALID_HHDM;
	if (!memmap_valid(memmap))
		return BOOT_INFO_INVALID_MEMMAP;

	if (framebuffers != NULL && framebuffers->framebuffer_count != 0) {
		if (framebuffers->framebuffers == NULL ||
		    framebuffers->framebuffer_count > SIZE_MAX /
		    sizeof(*framebuffers->framebuffers) ||
		    framebuffers->framebuffers[0] == NULL)
			return BOOT_INFO_INVALID_FRAMEBUFFER;
		framebuffer = framebuffers->framebuffers[0];
		if (!framebuffer_valid(framebuffer))
			return BOOT_INFO_INVALID_FRAMEBUFFER;
	}

	info->firmware_type = firmware->firmware_type;
	info->hhdm_offset = hhdm->offset;
	info->memmap = memmap;
	info->framebuffer = framebuffer;
	return BOOT_INFO_OK;
}

bool
bootstrap_allocator_init(struct bootstrap_allocator *allocator,
	const struct boot_info *info)
{
	if (allocator == NULL || info == NULL || info->memmap == NULL ||
	    info->hhdm_offset == 0 || !memmap_valid(info->memmap))
		return false;
	*allocator = (struct bootstrap_allocator){
		.memmap = info->memmap,
		.hhdm_offset = info->hhdm_offset,
		.next_region = 0,
		.next_physical = 0
	};
	return true;
}

void *
bootstrap_alloc(struct bootstrap_allocator *allocator, size_t size,
	size_t alignment)
{
	if (allocator == NULL || allocator->memmap == NULL || size == 0 ||
	    !is_power_of_two(alignment))
		return NULL;

	while (allocator->next_region < allocator->memmap->entry_count) {
		const struct limine_memmap_entry *region =
		    allocator->memmap->entries[allocator->next_region];
		uint64_t physical;
		uint64_t aligned;
		uint64_t allocation_end;
		uint64_t virtual_address;

		if (region->type != LIMINE_MEMMAP_USABLE) {
			allocator->next_region++;
			continue;
		}
		physical = allocator->next_physical > region->base ?
		    allocator->next_physical : region->base;
		if (physical > UINT64_MAX - (alignment - 1)) {
			allocator->next_region++;
			allocator->next_physical = 0;
			continue;
		}
		aligned = (physical + alignment - 1) & ~((uint64_t)alignment - 1);
		if (!range_end(aligned, size, &allocation_end) ||
		    allocation_end > region->base + region->length) {
			allocator->next_region++;
			allocator->next_physical = 0;
			continue;
		}
		if (aligned > UINT64_MAX - allocator->hhdm_offset) {
			allocator->next_region++;
			allocator->next_physical = 0;
			continue;
		}
		virtual_address = allocator->hhdm_offset + aligned;
		if (size - 1 > UINTPTR_MAX - (uintptr_t)virtual_address)
			return NULL;
		allocator->next_physical = allocation_end;
		return (void *)(uintptr_t)virtual_address;
	}
	return NULL;
}
