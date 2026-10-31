#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/vmm.h"

#define ELF_MAX_LOAD_SEGMENTS 16U

struct elf_segment_plan {
	uint64_t virtual_address;
	uint64_t file_offset;
	uint64_t file_size;
	uint64_t memory_size;
	uint64_t page_start;
	uint64_t page_count;
	uint32_t flags;
};

struct elf_load_plan {
	uint64_t entry;
	uint64_t program_header_address;
	uint16_t program_header_entry_size;
	uint16_t program_header_count;
	bool tls_present;
	uint64_t tls_image_address;
	uint64_t tls_file_offset;
	uint64_t tls_file_size;
	uint64_t tls_memory_size;
	uint64_t tls_alignment;
	uint64_t page_count;
	size_t segment_count;
	struct elf_segment_plan segments[ELF_MAX_LOAD_SEGMENTS];
};

struct elf_loaded_image {
	struct address_space address_space;
	uint64_t entry;
	uint64_t page_count;
	uint64_t *page_addresses;
	struct elf_load_plan plan;
};

bool elf_image_validate(const void *image, size_t image_size,
	struct elf_load_plan *plan);
bool elf_image_load(const void *image, size_t image_size,
	struct elf_loaded_image *loaded);
bool elf_image_unload(struct elf_loaded_image *loaded);

#endif
