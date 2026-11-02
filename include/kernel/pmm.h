#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine.h"
#include "kernel/boot.h"

bool pmm_init(const struct boot_info *info,
	struct bootstrap_allocator *bootstrap);
bool pmm_alloc_page(uint64_t *physical_address);
bool pmm_free_page(uint64_t physical_address);
bool pmm_alloc_contiguous(size_t page_count, size_t alignment_pages,
	uint64_t maximum_address_exclusive, uint64_t *physical_address);
bool pmm_free_contiguous(uint64_t physical_address, size_t page_count);
uint64_t pmm_total_pages(void);
/* Full usable frames, excluding reserved memory and physical address holes. */
uint64_t pmm_usable_pages(void);
uint64_t pmm_free_pages(void);
#if defined(KERNEL_TEST_PRIVATE_MAPPING_LIFECYCLE) || \
	defined(KERNEL_TEST_PRIVATE_MAPPING_PROTECTION)
bool pmm_test_page_is_allocated(uint64_t physical_address);
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_EXHAUSTION
void pmm_test_reset_exhaustion_failures(void);
uint64_t pmm_test_exhaustion_failures(void);
#endif

#endif
