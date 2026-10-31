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
#define ELF_PROGRAM_PHDR 6U
#define ELF_PROGRAM_GNU_STACK 0x6474e551U
#define ELF_FLAG_EXECUTE 1U
#define ELF_FLAG_WRITE 2U
#define ELF_FLAG_READ 4U
#define ELF_PAGE_SIZE 4096U
#define ELF_USER_BASE 0x10000ULL
#define ELF_USER_END 0x00007fffffffc000ULL
#define ELF_USER_STACK_START 0x00007fffffffd000ULL
#define ELF_USER_STACK_END 0x00007fffffffe000ULL

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
	loaded->address_space.private_map_cursor = 0;
	loaded->address_space.private_mappings = NULL;
	loaded->address_space.next = NULL;
	loaded->entry = 0;
	loaded->plan.program_header_address = 0;
	loaded->plan.program_header_entry_size = 0;
	loaded->plan.program_header_count = 0;
	loaded->plan.tls_present = false;
	loaded->plan.tls_image_address = 0;
	loaded->plan.tls_file_offset = 0;
	loaded->plan.tls_file_size = 0;
	loaded->plan.tls_memory_size = 0;
	loaded->plan.tls_alignment = 0;
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
	destination->program_header_address = source->program_header_address;
	destination->program_header_entry_size =
	    source->program_header_entry_size;
	destination->program_header_count = source->program_header_count;
	destination->tls_present = source->tls_present;
	destination->tls_image_address = source->tls_image_address;
	destination->tls_file_offset = source->tls_file_offset;
	destination->tls_file_size = source->tls_file_size;
	destination->tls_memory_size = source->tls_memory_size;
	destination->tls_alignment = source->tls_alignment;
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
	uint64_t program_table_size;
	uint64_t phdr_virtual_address = 0;
	uint64_t phdr_file_size = 0;
	uint64_t phdr_memory_size = 0;
	uint64_t phdr_offset = 0;
	uint64_t phdr_alignment = 0;
	uint32_t phdr_flags = 0;
	uint64_t tls_virtual_address = 0;
	uint64_t tls_file_offset = 0;
	uint64_t tls_file_size = 0;
	uint64_t tls_memory_size = 0;
	uint64_t tls_alignment = 0;
	uint32_t tls_flags = 0;
	uint16_t program_count;
	uint16_t i;
	bool entry_executable = false;
	bool has_phdr_segment = false;
	bool has_tls_segment = false;

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
	program_table_size = (uint64_t)program_count * ELF_PROGRAM_HEADER_SIZE;
	if (program_count == 0 || !range_in_file(program_offset,
	    program_table_size, image_size))
		return false;
	plan->entry = read_u64(bytes + 24);
	plan->program_header_address = 0;
	plan->program_header_entry_size = ELF_PROGRAM_HEADER_SIZE;
	plan->program_header_count = program_count;
	plan->tls_present = false;
	plan->tls_image_address = 0;
	plan->tls_file_offset = 0;
	plan->tls_file_size = 0;
	plan->tls_memory_size = 0;
	plan->tls_alignment = 0;
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

		if (type == ELF_PROGRAM_DYNAMIC || type == ELF_PROGRAM_INTERPRETER)
			return false;
		if (type == ELF_PROGRAM_TLS) {
			if (has_tls_segment)
				return false;
			has_tls_segment = true;
			tls_virtual_address = virtual_address;
			tls_file_offset = offset;
			tls_file_size = file_size;
			tls_memory_size = memory_size;
			tls_alignment = alignment;
			tls_flags = flags;
			continue;
		}
		if (type == ELF_PROGRAM_PHDR) {
			if (has_phdr_segment)
				return false;
			has_phdr_segment = true;
			phdr_offset = offset;
			phdr_virtual_address = virtual_address;
			phdr_file_size = file_size;
			phdr_memory_size = memory_size;
			phdr_flags = flags;
			phdr_alignment = alignment;
			continue;
		}
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
		    (page_start < PRIVATE_MAP_ARENA_END &&
		    PRIVATE_MAP_ARENA_START < page_end) ||
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
	if (plan->segment_count == 0 || !entry_executable)
		return false;
	if (has_phdr_segment && (phdr_offset != program_offset ||
	    phdr_file_size < program_table_size ||
	    phdr_memory_size < phdr_file_size ||
	    (phdr_flags & ELF_FLAG_READ) == 0 || (phdr_flags & ~7U) != 0 ||
	    !range_in_file(phdr_offset, phdr_file_size, image_size) ||
	    phdr_virtual_address > UINT64_MAX - phdr_memory_size ||
	    (phdr_alignment > 1 && (!power_of_two(phdr_alignment) ||
	    (phdr_offset & (phdr_alignment - 1)) !=
	    (phdr_virtual_address & (phdr_alignment - 1))))))
		return false;
	if (has_tls_segment) {
		uint64_t tls_end;
		size_t tls_matches = 0;
		size_t j;

		/* musl 1.2.6 uses p_align - 1 directly; zero is unsafe there. */
		if (tls_alignment == 0 || !power_of_two(tls_alignment) ||
		    tls_file_size > tls_memory_size ||
		    !range_in_file(tls_file_offset, tls_file_size, image_size) ||
		    (tls_flags & ~7U) != 0 || (tls_flags & ELF_FLAG_READ) == 0 ||
		    (tls_flags & ELF_FLAG_EXECUTE) != 0)
			return false;
		/* Linkers emit an empty PT_TLS when the PHDR is declared globally. */
		if (tls_memory_size == 0) {
			if (tls_file_size != 0)
				return false;
		} else {
			if (tls_virtual_address < ELF_USER_BASE ||
			    tls_virtual_address > ELF_USER_END ||
			    tls_memory_size > ELF_USER_END - tls_virtual_address ||
			    (tls_virtual_address & (tls_alignment - 1)) !=
			    (tls_file_offset & (tls_alignment - 1)))
				return false;
			tls_end = tls_virtual_address + tls_memory_size;
			if (tls_virtual_address < PRIVATE_MAP_ARENA_END &&
			    PRIVATE_MAP_ARENA_START < tls_end)
				return false;
			if (tls_virtual_address < SHM_ARENA_END &&
			    SHM_ARENA_START < tls_end)
				return false;
			if (tls_virtual_address < ELF_USER_STACK_END &&
			    ELF_USER_STACK_START < tls_end)
				return false;
			if (tls_file_size != 0) {
				for (j = 0; j < plan->segment_count; j++) {
					const struct elf_segment_plan *segment =
					    &plan->segments[j];
					uint64_t offset_in_segment;

					if ((segment->flags & ELF_FLAG_READ) == 0 ||
					    tls_file_offset < segment->file_offset ||
					    tls_virtual_address < segment->virtual_address)
						continue;
					offset_in_segment = tls_file_offset -
					    segment->file_offset;
					if (offset_in_segment != tls_virtual_address -
					    segment->virtual_address ||
					    offset_in_segment > segment->file_size ||
					    tls_file_size > segment->file_size - offset_in_segment ||
					    offset_in_segment > segment->memory_size ||
					    tls_file_size > segment->memory_size - offset_in_segment)
						continue;
					tls_matches++;
				}
				if (tls_matches != 1)
					return false;
			}
			plan->tls_present = true;
			plan->tls_image_address = tls_virtual_address;
			plan->tls_file_offset = tls_file_offset;
			plan->tls_file_size = tls_file_size;
			plan->tls_memory_size = tls_memory_size;
			plan->tls_alignment = tls_alignment;
		}
	}
	{
		size_t matches = 0;
		size_t j;
		uint64_t candidate = 0;

		for (j = 0; j < plan->segment_count; j++) {
			const struct elf_segment_plan *segment = &plan->segments[j];
			uint64_t offset_in_segment;
			uint64_t address;

			if ((segment->flags & ELF_FLAG_READ) == 0 ||
			    program_offset < segment->file_offset)
				continue;
			offset_in_segment = program_offset - segment->file_offset;
			if (offset_in_segment > segment->file_size ||
			    program_table_size > segment->file_size - offset_in_segment ||
			    offset_in_segment > segment->memory_size ||
			    program_table_size > segment->memory_size - offset_in_segment ||
			    offset_in_segment > UINT64_MAX - segment->virtual_address)
				continue;
			address = segment->virtual_address + offset_in_segment;
			if (address > UINT64_MAX - program_table_size)
				continue;
			if (has_phdr_segment && address != phdr_virtual_address)
				continue;
			candidate = address;
			matches++;
		}
		if (matches != 1)
			return false;
		plan->program_header_address = candidate;
	}
	return true;
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
