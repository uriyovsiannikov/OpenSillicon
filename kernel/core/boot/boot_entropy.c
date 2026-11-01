#include <stdbool.h>

#include "kernel/boot_entropy.h"

#define BOOT_ENTROPY_MAX_MEMMAP_ENTRIES 512U

static void
clear_words(uint64_t words[BOOT_ENTROPY_MAX_VALUES])
{
	volatile uint64_t *current = words;
	for (size_t i = 0; i < BOOT_ENTROPY_MAX_VALUES; i++)
		current[i] = 0;
}

static bool
range_fits_type(const struct limine_memmap_response *memmap,
	uint64_t physical, uint64_t length, uint64_t required_type)
{
	uint64_t end;
	uintptr_t entries_address;

	if (memmap == NULL || memmap->entries == NULL ||
	    memmap->entry_count == 0 || memmap->entry_count >
	    BOOT_ENTROPY_MAX_MEMMAP_ENTRIES || length == 0 ||
	    physical > UINT64_MAX - length ||
	    memmap->entry_count > SIZE_MAX / sizeof(*memmap->entries))
		return false;
	entries_address = (uintptr_t)memmap->entries;
	if (memmap->entry_count > (UINTPTR_MAX - entries_address) /
	    sizeof(*memmap->entries))
		return false;
	end = physical + length;
	for (uint64_t i = 0; i < memmap->entry_count; i++) {
		const struct limine_memmap_entry *entry = memmap->entries[i];
		uint64_t entry_end;

		if (entry == NULL || entry->length == 0 ||
		    entry->base > UINT64_MAX - entry->length)
			return false;
		entry_end = entry->base + entry->length;
		if (entry->type == required_type && physical >= entry->base &&
		    end <= entry_end)
			return true;
	}
	return false;
}

static bool
hhdm_range_fits_type(const void *pointer, uint64_t length,
	uint64_t hhdm_offset, const struct limine_memmap_response *memmap,
	uint64_t required_type)
{
	uintptr_t address = (uintptr_t)pointer;
	uint64_t physical;

	if (pointer == NULL || address < hhdm_offset || length == 0 ||
	    length - 1 > UINTPTR_MAX - address)
		return false;
	physical = address - hhdm_offset;
	return range_fits_type(memmap, physical, length, required_type);
}

enum boot_entropy_result
boot_entropy_inspect(const struct limine_entropy_response *response,
	uint64_t hhdm_offset, const struct limine_memmap_response *memmap,
	uint64_t output[BOOT_ENTROPY_MAX_VALUES], size_t *value_count)
{
	uint64_t count;
	uint64_t values_bytes;
	const uint64_t *values;

	if (output != NULL)
		clear_words(output);
	if (value_count == NULL || output == NULL)
		return BOOT_ENTROPY_INVALID;
	*value_count = 0;
	if (response == NULL)
		return BOOT_ENTROPY_MISSING;
	if (!hhdm_range_fits_type(response, sizeof(*response), hhdm_offset,
	    memmap, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE) ||
	    response->revision != 0)
		return BOOT_ENTROPY_INVALID;

	count = response->value_count;
	if (count > BOOT_ENTROPY_MAX_VALUES)
		return BOOT_ENTROPY_INVALID;
	if (count == 0)
		return response->values == NULL ? BOOT_ENTROPY_EMPTY :
		    BOOT_ENTROPY_INVALID;
	if (response->values == NULL || count > UINT64_MAX / sizeof(*values))
		return BOOT_ENTROPY_INVALID;
	values_bytes = count * sizeof(*values);
	if (!hhdm_range_fits_type(response->values, values_bytes, hhdm_offset,
	    memmap, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE))
		return BOOT_ENTROPY_INVALID;

	values = response->values;
	for (uint64_t i = 0; i < count; i++)
		output[i] = values[i];
	*value_count = (size_t)count;
	return count < BOOT_ENTROPY_MAX_VALUES ?
	    BOOT_ENTROPY_PARTIAL_UNTRUSTED :
	    BOOT_ENTROPY_AVAILABLE_UNTRUSTED;
}
