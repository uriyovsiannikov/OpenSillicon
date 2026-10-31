#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/elf.h"
#include "kernel/shm.h"

#define TEST_IMAGE_SIZE 256U
#define PROGRAM_HEADER_OFFSET 64U
#define SEGMENT_OFFSET 128U
#define TEST_MAPPINGS 32U

struct test_mapping {
	uint64_t virtual_address;
	uint64_t physical_address;
	unsigned int flags;
};

static struct test_mapping mappings[TEST_MAPPINGS];
static size_t mapping_count;
static size_t allocated_pages;
static struct address_space *active_space;
static bool fail_page_allocation;
static bool fail_page_mapping;

void *
kheap_alloc(size_t size)
{
	return malloc(size);
}

bool
kheap_free(void *allocation)
{
	free(allocation);
	return true;
}

bool
pmm_alloc_page(uint64_t *physical_address)
{
	void *page = aligned_alloc(4096, 4096);

	if (fail_page_allocation || page == NULL) {
		free(page);
		return false;
	}
	*physical_address = (uintptr_t)page;
	allocated_pages++;
	return true;
}

bool
pmm_free_page(uint64_t physical_address)
{
	free((void *)(uintptr_t)physical_address);
	assert(allocated_pages != 0);
	allocated_pages--;
	return true;
}

uint64_t
vmm_hhdm_offset(void)
{
	return 0;
}

bool
address_space_create(struct address_space *space)
{
	space->root_physical = 1;
	space->next = NULL;
	return true;
}

bool
vmm_map_page(struct address_space *space, uint64_t virtual_address,
	uint64_t physical_address, unsigned int flags)
{
	if (fail_page_mapping)
		return false;
	assert(mapping_count < TEST_MAPPINGS);
	mappings[mapping_count++] = (struct test_mapping){
		.virtual_address = virtual_address,
		.physical_address = physical_address,
		.flags = flags
	};
	(void)space;
	return true;
}

bool
vmm_unmap_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address)
{
	size_t i;

	(void)space;
	for (i = 0; i < mapping_count; i++) {
		if (mappings[i].virtual_address == virtual_address) {
			*physical_address = mappings[i].physical_address;
			mappings[i] = mappings[--mapping_count];
			return true;
		}
	}
	return false;
}

bool
address_space_destroy(struct address_space *space)
{
	space->root_physical = 0;
	space->next = NULL;
	return true;
}

bool
address_space_activate(const struct address_space *space)
{
	active_space = (struct address_space *)space;
	return true;
}

struct address_space *
vmm_active_address_space(void)
{
	return active_space;
}

static void
put16(uint8_t *buffer, size_t offset, uint16_t value)
{
	buffer[offset] = value;
	buffer[offset + 1] = value >> 8;
}

static void
put32(uint8_t *buffer, size_t offset, uint32_t value)
{
	buffer[offset] = value;
	buffer[offset + 1] = value >> 8;
	buffer[offset + 2] = value >> 16;
	buffer[offset + 3] = value >> 24;
}

static void
put64(uint8_t *buffer, size_t offset, uint64_t value)
{
	put32(buffer, offset, value);
	put32(buffer, offset + 4, value >> 32);
}

static void
make_test_image(uint8_t image[TEST_IMAGE_SIZE])
{
	memset(image, 0, TEST_IMAGE_SIZE);
	image[0] = 0x7f;
	image[1] = 'E';
	image[2] = 'L';
	image[3] = 'F';
	image[4] = 2;
	image[5] = 1;
	image[6] = 1;
	put16(image, 16, 2);
	put16(image, 18, 62);
	put32(image, 20, 1);
	put64(image, 24, 0x400080);
	put64(image, 32, PROGRAM_HEADER_OFFSET);
	put16(image, 52, 64);
	put16(image, 54, 56);
	put16(image, 56, 1);
	put32(image, PROGRAM_HEADER_OFFSET, 1);
	put32(image, PROGRAM_HEADER_OFFSET + 4, 5);
	put64(image, PROGRAM_HEADER_OFFSET + 8, SEGMENT_OFFSET);
	put64(image, PROGRAM_HEADER_OFFSET + 16, 0x400080);
	put64(image, PROGRAM_HEADER_OFFSET + 32, 4);
	put64(image, PROGRAM_HEADER_OFFSET + 40, 16);
	put64(image, PROGRAM_HEADER_OFFSET + 48, 4096);
	memcpy(image + SEGMENT_OFFSET, "ELF!", 4);
}

static void
expect_invalid(uint8_t *image)
{
	struct elf_load_plan plan;

	assert(!elf_image_validate(image, TEST_IMAGE_SIZE, &plan));
}

int
main(void)
{
	uint8_t image[TEST_IMAGE_SIZE];
	struct elf_load_plan plan;
	struct elf_loaded_image loaded;
	uint8_t *loaded_page;

	make_test_image(image);
	assert(elf_image_validate(image, sizeof(image), &plan));
	assert(plan.entry == 0x400080 && plan.segment_count == 1 &&
	    plan.page_count == 1 && plan.segments[0].file_size == 4 &&
	    plan.segments[0].memory_size == 16);
	assert(elf_image_load(image, sizeof(image), &loaded));
	assert(mapping_count == 1 && allocated_pages == 1 &&
	    mappings[0].virtual_address == 0x400000 &&
	    mappings[0].flags == (VMM_USER | VMM_EXEC));
	loaded_page = (uint8_t *)(uintptr_t)mappings[0].physical_address;
	assert(memcmp(loaded_page + 0x80, "ELF!", 4) == 0);
	assert(loaded_page[0x84] == 0 && loaded_page[0] == 0);
	assert(elf_image_unload(&loaded));
	assert(mapping_count == 0 && allocated_pages == 0);

	make_test_image(image);
	fail_page_allocation = true;
	assert(!elf_image_load(image, sizeof(image), &loaded));
	fail_page_allocation = false;
	assert(mapping_count == 0 && allocated_pages == 0);
	fail_page_mapping = true;
	assert(!elf_image_load(image, sizeof(image), &loaded));
	fail_page_mapping = false;
	assert(mapping_count == 0 && allocated_pages == 0);

	make_test_image(image);
	image[0] = 0;
	expect_invalid(image);
	make_test_image(image);
	put16(image, 18, 3);
	expect_invalid(image);
	make_test_image(image);
	put64(image, 32, UINT64_MAX - 8);
	expect_invalid(image);
	make_test_image(image);
	put64(image, PROGRAM_HEADER_OFFSET + 32, 17);
	expect_invalid(image);
	make_test_image(image);
	put64(image, PROGRAM_HEADER_OFFSET + 16, 0x8000);
	put64(image, 24, 0x8000);
	expect_invalid(image);
	make_test_image(image);
	put32(image, PROGRAM_HEADER_OFFSET + 4, 7);
	expect_invalid(image);
	make_test_image(image);
	put64(image, 24, 0x400000);
	expect_invalid(image);
	make_test_image(image);
	put64(image, PROGRAM_HEADER_OFFSET + 16, SHM_ARENA_START + 128);
	put64(image, 24, SHM_ARENA_START + 128);
	expect_invalid(image);
	make_test_image(image);
	put32(image, PROGRAM_HEADER_OFFSET, 3);
	expect_invalid(image);

	puts("ELF64 validation, load, and cleanup tests passed");
	return 0;
}
