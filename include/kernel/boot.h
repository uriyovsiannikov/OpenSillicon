#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine.h"

enum boot_info_error {
	BOOT_INFO_OK = 0,
	BOOT_INFO_MISSING_RESPONSE,
	BOOT_INFO_INVALID_FIRMWARE,
	BOOT_INFO_INVALID_HHDM,
	BOOT_INFO_INVALID_MEMMAP,
	BOOT_INFO_INVALID_FRAMEBUFFER,
	BOOT_INFO_INVALID_MODULE
};

struct boot_info {
	uint64_t firmware_type;
	uint64_t hhdm_offset;
	const struct limine_memmap_response *memmap;
	const struct limine_framebuffer *framebuffer;
	const struct limine_file *initramfs;
};

enum boot_info_error boot_info_parse(
	const struct limine_firmware_type_response *firmware,
	const struct limine_hhdm_response *hhdm,
	const struct limine_memmap_response *memmap,
	const struct limine_framebuffer_response *framebuffers,
	const struct limine_module_response *modules,
	struct boot_info *info);

struct bootstrap_allocator {
	const struct limine_memmap_response *memmap;
	uint64_t hhdm_offset;
	size_t next_region;
	uint64_t next_physical;
};

bool bootstrap_allocator_init(struct bootstrap_allocator *allocator,
	const struct boot_info *info);
void *bootstrap_alloc(struct bootstrap_allocator *allocator, size_t size,
	size_t alignment);

#endif
