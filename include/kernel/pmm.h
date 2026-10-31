#ifndef KERNEL_PMM_H
#define KERNEL_PMM_H

#include <stdbool.h>
#include <stdint.h>

#include "limine.h"
#include "kernel/boot.h"

bool pmm_init(const struct boot_info *info,
	struct bootstrap_allocator *bootstrap);
bool pmm_alloc_page(uint64_t *physical_address);
bool pmm_free_page(uint64_t physical_address);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);

#endif
