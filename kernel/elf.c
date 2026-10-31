#include <stdint.h>

#include "kernel/elf.h"
#include "kernel/heap.h"
#include "kernel/pmm.h"
#include "kernel/shm.h"
#include "kernel/vmm.h"

#define ELF_HEADER_SIZE 64U
#define ELF_PROGRAM_HEADER_SIZE 56U
#define ELF_TYPE_EXECUTABLE 2U
#define ELF_MACHINE_X86_64 62U
#define ELF_PROGRAM_LOAD 1U
#define ELF_PROGRAM_DYNAMIC 2U
#define ELF_PROGRAM_INTERPRETER 3U
#define ELF_PROGRAM_TLS 7U
#define ELF_PROGRAM_GNU_STACK 0x6474e551U
#define ELF_FLAG_EXECUTE 1U
#define ELF_FLAG_WRITE 2U
#define ELF_FLAG_READ 4U
#define ELF_PAGE_SIZE 4096U
#define ELF_USER_BASE 0x10000ULL
#define ELF_USER_END 0x00007fffffffc000ULL

static uint16_t
read_u16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t
read_u32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
	    (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t
read_u64(const uint8_t *bytes)
{
	return (uint64_t)read_u32(bytes) | (uint64_t)read_u32(bytes + 4) << 32;
}

static bool
range_in_file(uint64_t offset, uint64_t length, size_t image_size)
{
	return offset <= image_size && length <= image_size - offset;
}

static bool
power_of_two(uint64_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

static void
reset_loaded_image(struct elf_loaded_image *loaded)
{
	loaded->address_space.root_physical = 0;
	loaded->address_space.shm_next_address = 0;
	loaded->address_space.shared_mappings.head = NULL;
	loaded->address_space.next = NULL;
	loaded->entry = 0;
	loaded->page_count = 0;
	loaded->page_addresses = NULL;
	loaded->plan.entry = 0;
	loaded->plan.page_count = 0;
	loaded->plan.segment_count = 0;
}

static void
copy_plan(struct elf_load_plan *destination, const struct elf_load_plan *source)
{
	size_t i;

	destination->entry = source->entry;
	destination->page_count = source->page_count;
	destination->segment_count = source->segment_count;
	for (i = 0; i < source->segment_count; i++) {
		destination->segments[i].virtual_address =
		    source->segments[i].virtual_address;
		destination->segments[i].file_offset = source->segments[i].file_offset;
		destination->segments[i].file_size = source->segments[i].file_size;
		destination->segments[i].memory_size = source->segments[i].memory_size;
		destination->segments[i].page_start = source->segments[i].page_start;
		destination->segments[i].page_count = source->segments[i].page_count;
		destination->segments[i].flags = source->segments[i].flags;
	}
}

bool
elf_image_validate(const void *image, size_t image_size,
	struct elf_load_plan *plan)
{
	const uint8_t *bytes = image;
	uint64_t program_offset;
	uint16_t program_count;
	uint16_t i;
	bool entry_executable = false;

	if (bytes == NULL || plan == NULL || image_size < ELF_HEADER_SIZE)
		return false;
	if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
	    bytes[3] != 'F' || bytes[4] != 2 || bytes[5] != 1 ||
	    bytes[6] != 1 || bytes[7] != 0 ||
	    read_u16(bytes + 16) != ELF_TYPE_EXECUTABLE ||
	    read_u16(bytes + 18) != ELF_MACHINE_X86_64 ||
	    read_u32(bytes + 20) != 1 || read_u16(bytes + 52) != ELF_HEADER_SIZE ||
	    read_u16(bytes + 54) != ELF_PROGRAM_HEADER_SIZE)
		return false;
	program_offset = read_u64(bytes + 32);
	program_count = read_u16(bytes + 56);
	if (program_count == 0 || !range_in_file(program_offset,
	    (uint64_t)program_count * ELF_PROGRAM_HEADER_SIZE, image_size))
		return false;
	plan->entry = read_u64(bytes + 24);
	plan->page_count = 0;
	plan->segment_count = 0;
	for (i = 0; i < program_count; i++) {
		const uint8_t *header = bytes + program_offset +
		    (uint64_t)i * ELF_PROGRAM_HEADER_SIZE;
		uint32_t type = read_u32(header);
		uint32_t flags = read_u32(header + 4);
		uint64_t offset = read_u64(header + 8);
		uint64_t virtual_address = read_u64(header + 16);
		uint64_t file_size = read_u64(header + 32);
		uint64_t memory_size = read_u64(header + 40);
		uint64_t alignment = read_u64(header + 48);
		uint64_t end;
		uint64_t page_start;
		uint64_t page_end;
		uint64_t page_count;
		size_t j;

		if (type == ELF_PROGRAM_DYNAMIC || type == ELF_PROGRAM_INTERPRETER ||
		    type == ELF_PROGRAM_TLS)
			return false;
		if (type != ELF_PROGRAM_LOAD)
			continue;
		if (plan->segment_count == ELF_MAX_LOAD_SEGMENTS || memory_size == 0 ||
		    file_size > memory_size || (flags & ~7U) != 0 ||
		    (flags & ELF_FLAG_READ) == 0 ||
		    (flags & (ELF_FLAG_WRITE | ELF_FLAG_EXECUTE)) ==
		    (ELF_FLAG_WRITE | ELF_FLAG_EXECUTE) ||
		    !range_in_file(offset, file_size, image_size) ||
		    (virtual_address & (ELF_PAGE_SIZE - 1)) !=
		    (offset & (ELF_PAGE_SIZE - 1)) ||
		    (alignment > 1 && (!power_of_two(alignment) ||
		    (virtual_address & (alignment - 1)) !=
		    (offset & (alignment - 1)))) ||
		    memory_size > UINT64_MAX - virtual_address)
			return false;
		end = virtual_address + memory_size;
		page_start = virtual_address & ~(uint64_t)(ELF_PAGE_SIZE - 1);
		if (end > UINT64_MAX - (ELF_PAGE_SIZE - 1))
			return false;
		page_end = (end + ELF_PAGE_SIZE - 1) &
		    ~(uint64_t)(ELF_PAGE_SIZE - 1);
		if (page_start < ELF_USER_BASE || page_end > ELF_USER_END ||
		    page_end <= page_start ||
		    (page_start < SHM_ARENA_END && SHM_ARENA_START < page_end))
			return false;
		page_count = (page_end - page_start) / ELF_PAGE_SIZE;
		for (j = 0; j < plan->segment_count; j++) {
			const struct elf_segment_plan *previous = &plan->segments[j];
			uint64_t previous_end = previous->page_start +
			    previous->page_count * ELF_PAGE_SIZE;

			if (page_start < previous_end && previous->page_start < page_end)
				return false;
		}
		if (page_count > UINT64_MAX - plan->page_count)
			return false;
		plan->segments[plan->segment_count++] = (struct elf_segment_plan){
			.virtual_address = virtual_address,
			.file_offset = offset,
			.file_size = file_size,
			.memory_size = memory_size,
			.page_start = page_start,
			.page_count = page_count,
			.flags = flags
		};
		plan->page_count += page_count;
		if ((flags & ELF_FLAG_EXECUTE) != 0 && plan->entry >= virtual_address &&
		    plan->entry < end)
			entry_executable = true;
	}
	return plan->segment_count != 0 && entry_executable;
}

static void
zero_page(uint64_t physical)
{
	volatile uint8_t *page = (volatile uint8_t *)(uintptr_t)(
	    vmm_hhdm_offset() + physical);
	uint64_t i;

	for (i = 0; i < ELF_PAGE_SIZE; i++)
		page[i] = 0;
}

static bool
hhdm_page_valid(uint64_t physical)
{
	uint64_t offset = vmm_hhdm_offset();
	uint64_t last_byte = ELF_PAGE_SIZE - 1;

	return offset <= UINT64_MAX - physical &&
	    last_byte <= UINT64_MAX - (offset + physical);
}

static bool
release_image_pages(struct address_space *space,
	const struct elf_load_plan *plan, uint64_t *pages, uint64_t allocated,
	uint64_t mapped)
{
	uint64_t page_index = 0;
	uint64_t mapped_left = mapped;
	uint64_t i;
	bool success = true;

	for (i = 0; i < plan->segment_count && mapped_left != 0; i++) {
		const struct elf_segment_plan *segment = &plan->segments[i];
		uint64_t j;

		for (j = 0; j < segment->page_count && mapped_left != 0; j++) {
			uint64_t physical;
			uint64_t virtual_address = segment->page_start +
			    j * ELF_PAGE_SIZE;

			if (!vmm_unmap_page(space, virtual_address, &physical) ||
			    physical != pages[page_index] || !pmm_free_page(physical))
				success = false;
			page_index++;
			mapped_left--;
		}
	}
	for (i = mapped; i < allocated; i++) {
		if (!pmm_free_page(pages[i]))
			success = false;
	}
	if (space != NULL && !address_space_destroy(space))
		success = false;
	return success;
}

bool
elf_image_load(const void *image, size_t image_size,
	struct elf_loaded_image *loaded)
{
	const uint8_t *bytes = image;
	struct elf_load_plan plan;
	uint64_t allocated = 0;
	uint64_t mapped = 0;
	uint64_t page_index = 0;
	size_t i;
	bool space_created = false;

	if (loaded == NULL || !elf_image_validate(image, image_size, &plan) ||
	    plan.page_count > SIZE_MAX / sizeof(uint64_t))
		return false;
	reset_loaded_image(loaded);
	loaded->page_addresses = kheap_alloc((size_t)plan.page_count *
	    sizeof(uint64_t));
	if (loaded->page_addresses == NULL)
		return false;
	while (allocated < plan.page_count) {
		if (!pmm_alloc_page(&loaded->page_addresses[allocated]))
			goto fail;
		allocated++;
		if (!hhdm_page_valid(loaded->page_addresses[allocated - 1]))
			goto fail;
		zero_page(loaded->page_addresses[allocated - 1]);
	}
	if (!address_space_create(&loaded->address_space))
		goto fail;
	space_created = true;
	for (i = 0; i < plan.segment_count; i++) {
		const struct elf_segment_plan *segment = &plan.segments[i];
		unsigned int flags = VMM_USER;
		uint64_t j;

		if ((segment->flags & ELF_FLAG_WRITE) != 0)
			flags |= VMM_WRITE;
		if ((segment->flags & ELF_FLAG_EXECUTE) != 0)
			flags |= VMM_EXEC;
		for (j = 0; j < segment->page_count; j++) {
			if (!vmm_map_page(&loaded->address_space,
			    segment->page_start + j * ELF_PAGE_SIZE,
			    loaded->page_addresses[page_index], flags))
				goto fail;
			mapped++;
			page_index++;
		}
	}
	page_index = 0;
	for (i = 0; i < plan.segment_count; i++) {
		const struct elf_segment_plan *segment = &plan.segments[i];
		uint64_t remaining = segment->file_size;
		uint64_t source = segment->file_offset;
		uint64_t destination = segment->virtual_address;

		while (remaining != 0) {
			uint64_t relative_page = (destination - segment->page_start) /
			    ELF_PAGE_SIZE;
			uint64_t page_offset = destination & (ELF_PAGE_SIZE - 1);
			uint64_t chunk = ELF_PAGE_SIZE - page_offset;
			volatile uint8_t *output;
			uint64_t j;

			if (chunk > remaining)
				chunk = remaining;
			output = (volatile uint8_t *)(uintptr_t)(vmm_hhdm_offset() +
			    loaded->page_addresses[page_index + relative_page] + page_offset);
			for (j = 0; j < chunk; j++)
				output[j] = bytes[source + j];
			source += chunk;
			destination += chunk;
			remaining -= chunk;
		}
		page_index += segment->page_count;
	}
	loaded->entry = plan.entry;
	loaded->page_count = plan.page_count;
	copy_plan(&loaded->plan, &plan);
	return true;

fail:
	if (!release_image_pages(space_created ? &loaded->address_space : NULL,
	    &plan, loaded->page_addresses, allocated, mapped))
		return false;
	(void)kheap_free(loaded->page_addresses);
	reset_loaded_image(loaded);
	return false;
}

bool
elf_image_unload(struct elf_loaded_image *loaded)
{
	uint64_t page_index = 0;
	size_t i;
	bool success = true;

	if (loaded == NULL || loaded->page_addresses == NULL ||
	    loaded->page_count == 0 ||
	    vmm_active_address_space() == &loaded->address_space)
		return false;
	for (i = 0; i < loaded->plan.segment_count; i++) {
		const struct elf_segment_plan *segment = &loaded->plan.segments[i];
		uint64_t j;

		for (j = 0; j < segment->page_count; j++) {
			uint64_t physical;

			if (!vmm_unmap_page(&loaded->address_space,
			    segment->page_start + j * ELF_PAGE_SIZE, &physical) ||
			    physical != loaded->page_addresses[page_index] ||
			    !pmm_free_page(physical))
				success = false;
			page_index++;
		}
	}
	if (!success || !address_space_destroy(&loaded->address_space))
		return false;
	if (!kheap_free(loaded->page_addresses))
		return false;
	reset_loaded_image(loaded);
	return true;
}
