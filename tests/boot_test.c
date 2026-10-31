#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/boot.h"

#define PHYS_BASE 0x100000ULL
#define TEST_MEMORY_SIZE 8192U

static _Alignas(4096) unsigned char test_memory[TEST_MEMORY_SIZE];
static struct limine_memmap_entry usable;
static struct limine_memmap_entry reserved;
static struct limine_memmap_entry *entries[2];
static struct limine_memmap_response memmap;
static struct limine_firmware_type_response firmware;
static struct limine_hhdm_response hhdm;
static struct limine_framebuffer framebuffer;
static struct limine_framebuffer *framebuffer_entries[1];
static struct limine_framebuffer_response framebuffers;

static void
setup(void)
{
	usable = (struct limine_memmap_entry){
		.base = PHYS_BASE,
		.length = TEST_MEMORY_SIZE,
		.type = LIMINE_MEMMAP_USABLE
	};
	reserved = (struct limine_memmap_entry){
		.base = PHYS_BASE + TEST_MEMORY_SIZE,
		.length = 4096,
		.type = LIMINE_MEMMAP_RESERVED
	};
	entries[0] = &usable;
	entries[1] = &reserved;
	memmap = (struct limine_memmap_response){
		.entry_count = 2,
		.entries = entries
	};
	firmware = (struct limine_firmware_type_response){
		.firmware_type = LIMINE_FIRMWARE_TYPE_EFI64
	};
	hhdm = (struct limine_hhdm_response){
		.offset = (uintptr_t)test_memory - PHYS_BASE
	};
	framebuffer = (struct limine_framebuffer){
		.address = test_memory,
		.width = 800,
		.height = 600,
		.pitch = 3200,
		.bpp = 32,
		.memory_model = LIMINE_FRAMEBUFFER_RGB
	};
	framebuffer_entries[0] = &framebuffer;
	framebuffers = (struct limine_framebuffer_response){
		.framebuffer_count = 1,
		.framebuffers = framebuffer_entries
	};
}

static struct boot_info
parse_valid(void)
{
	struct boot_info info;

	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_OK);
	return info;
}

static void
test_missing_responses(void)
{
	struct boot_info info;

	setup();
	assert(boot_info_parse(NULL, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_MISSING_RESPONSE);
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, NULL) ==
	    BOOT_INFO_MISSING_RESPONSE);
}

static void
test_firmware_and_hhdm_validation(void)
{
	struct boot_info info;

	setup();
	firmware.firmware_type = LIMINE_FIRMWARE_TYPE_X86BIOS;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FIRMWARE);
	setup();
	hhdm.offset++;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_HHDM);
}

static void
test_memmap_validation(void)
{
	struct boot_info info;

	setup();
	memmap.entry_count = 0;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MEMMAP);
	setup();
	reserved.base = usable.base + 2048;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MEMMAP);
	setup();
	usable.base = UINT64_MAX - 7;
	usable.length = 16;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MEMMAP);
}

static void
test_framebuffer_validation(void)
{
	struct boot_info info;

	setup();
	framebuffer.pitch = 100;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FRAMEBUFFER);
	setup();
	framebuffers.framebuffers = NULL;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FRAMEBUFFER);
	setup();
	framebuffer.height = UINT64_MAX;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FRAMEBUFFER);
	setup();
	framebuffers.framebuffer_count = 0;
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_OK);
	assert(info.framebuffer == NULL);
}

static void
test_bootstrap_allocator(void)
{
	struct boot_info info;
	struct bootstrap_allocator allocator;
	void *first;
	void *second;

	setup();
	info = parse_valid();
	assert(bootstrap_allocator_init(&allocator, &info));
	first = bootstrap_alloc(&allocator, 4096, 4096);
	second = bootstrap_alloc(&allocator, 4096, 4096);
	assert(first == test_memory);
	assert(second == test_memory + 4096);
	assert(bootstrap_alloc(&allocator, 1, 1) == NULL);
	assert(bootstrap_alloc(&allocator, 1, 3) == NULL);
	assert(bootstrap_alloc(&allocator, 0, 1) == NULL);
	info.hhdm_offset = UINT64_MAX - 4095;
	assert(bootstrap_allocator_init(&allocator, &info));
	assert(bootstrap_alloc(&allocator, 16, 16) == NULL);
}

int
main(void)
{
	test_missing_responses();
	test_firmware_and_hhdm_validation();
	test_memmap_validation();
	test_framebuffer_validation();
	test_bootstrap_allocator();
	puts("boot information and bootstrap allocator tests passed");
	return 0;
}
