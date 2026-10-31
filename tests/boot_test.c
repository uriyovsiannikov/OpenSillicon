#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/boot.h"
#include "kernel/pmm.h"

#define BOOT_PARSE(firmware, hhdm, memmap, framebuffers, info) \
	boot_info_parse(firmware, hhdm, memmap, framebuffers, &modules, &rsdp, info)

#define PHYS_BASE 0x100000ULL
#define TEST_MEMORY_SIZE 8192U

static _Alignas(4096) unsigned char test_memory[TEST_MEMORY_SIZE];
static struct limine_memmap_entry usable;
static struct limine_memmap_entry reserved;
static struct limine_memmap_entry usable_high;
static struct limine_memmap_entry reserved_tail;
static struct limine_memmap_entry *entries[4];
static struct limine_memmap_response memmap;
static struct limine_firmware_type_response firmware;
static struct limine_hhdm_response hhdm;
static struct limine_framebuffer framebuffer;
static struct limine_framebuffer *framebuffer_entries[1];
static struct limine_framebuffer_response framebuffers;
static struct limine_file initramfs;
static struct limine_file *module_entries[65];
static struct limine_module_response modules;
static struct limine_rsdp_response rsdp;
static const char initramfs_name[] = "initramfs";
static struct limine_file duplicate_module;

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
	initramfs = (struct limine_file){
		.address = test_memory,
		.size = sizeof(test_memory),
		.string = (char *)initramfs_name
	};
	module_entries[0] = &initramfs;
	modules = (struct limine_module_response){
		.module_count = 1,
		.modules = module_entries
	};
	rsdp = (struct limine_rsdp_response){
		.address = (void *)(uintptr_t)PHYS_BASE
	};
}

static struct boot_info
parse_valid(void)
{
	struct boot_info info;

	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_OK);
	assert(info.initramfs == &initramfs);
	assert(info.rsdp_physical == PHYS_BASE);
	return info;
}

static void
test_missing_responses(void)
{
	struct boot_info info;

	setup();
	assert(BOOT_PARSE(NULL, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_MISSING_RESPONSE);
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, NULL) ==
	    BOOT_INFO_MISSING_RESPONSE);
	assert(boot_info_parse(&firmware, &hhdm, &memmap, &framebuffers, NULL,
	    &rsdp, &info) == BOOT_INFO_INVALID_MODULE);
}

static void
test_module_validation(void)
{
	struct boot_info info;

	setup();
	modules.module_count = 0;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
	setup();
	initramfs.string = "other";
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
	setup();
	initramfs.size = 0;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
	setup();
	initramfs.address = NULL;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
	setup();
	initramfs.address = (void *)(uintptr_t)(UINTPTR_MAX - 3);
	initramfs.size = 8;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
	setup();
	duplicate_module = initramfs;
	module_entries[1] = &duplicate_module;
	modules.module_count = 2;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
	setup();
	modules.module_count = 65;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MODULE);
}

static void
test_firmware_and_hhdm_validation(void)
{
	struct boot_info info;

	setup();
	firmware.firmware_type = LIMINE_FIRMWARE_TYPE_X86BIOS;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FIRMWARE);
	setup();
	hhdm.offset++;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_HHDM);
}

static void
test_memmap_validation(void)
{
	struct boot_info info;

	setup();
	memmap.entry_count = 0;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MEMMAP);
	setup();
	reserved.base = usable.base + 2048;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MEMMAP);
	setup();
	usable.base = UINT64_MAX - 7;
	usable.length = 16;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, NULL, &info) ==
	    BOOT_INFO_INVALID_MEMMAP);
}

static void
test_framebuffer_validation(void)
{
	struct boot_info info;

	setup();
	framebuffer.pitch = 100;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FRAMEBUFFER);
	setup();
	framebuffers.framebuffers = NULL;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FRAMEBUFFER);
	setup();
	framebuffer.height = UINT64_MAX;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
	    BOOT_INFO_INVALID_FRAMEBUFFER);
	setup();
	framebuffers.framebuffer_count = 0;
	assert(BOOT_PARSE(&firmware, &hhdm, &memmap, &framebuffers, &info) ==
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

static void
test_physical_memory_manager(void)
{
	struct boot_info info;
	struct bootstrap_allocator allocator;
	uint64_t physical_address = 0;

	setup();
	usable.length = 4096;
	reserved.base = PHYS_BASE + 4096;
	reserved.length = 4096;
	usable_high = (struct limine_memmap_entry){
		.base = PHYS_BASE + 8192,
		.length = 4096,
		.type = LIMINE_MEMMAP_USABLE
	};
	reserved_tail = (struct limine_memmap_entry){
		.base = PHYS_BASE + 12288,
		.length = 4096,
		.type = LIMINE_MEMMAP_RESERVED
	};
	entries[0] = &usable;
	entries[1] = &reserved;
	entries[2] = &usable_high;
	entries[3] = &reserved_tail;
	memmap.entry_count = 4;
	info = parse_valid();
	assert(bootstrap_allocator_init(&allocator, &info));
	assert(pmm_init(&info, &allocator));
	assert(pmm_total_pages() == (PHYS_BASE + 12288) / 4096);
	assert(pmm_free_pages() == 1);
	assert(!pmm_alloc_page(NULL));
	assert(pmm_alloc_page(&physical_address));
	assert(physical_address == PHYS_BASE + 8192);
	assert(pmm_free_pages() == 0);
	assert(pmm_free_page(physical_address));
	assert(!pmm_free_page(physical_address));
	assert(!pmm_free_page(physical_address + 1));
	assert(!pmm_free_page(PHYS_BASE));
	assert(!pmm_free_page(PHYS_BASE + 4096));
	assert(pmm_free_pages() == 1);

	setup();
	usable.length = 4096;
	info = parse_valid();
	assert(bootstrap_allocator_init(&allocator, &info));
	assert(pmm_init(&info, &allocator));
	assert(pmm_free_pages() == 0);
	assert(!pmm_alloc_page(&physical_address));
}

int
main(void)
{
	test_missing_responses();
	test_module_validation();
	test_firmware_and_hhdm_validation();
	test_memmap_validation();
	test_framebuffer_validation();
	test_bootstrap_allocator();
	test_physical_memory_manager();
	puts("boot information, bootstrap allocator, and PMM tests passed");
	return 0;
}
