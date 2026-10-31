#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/boot.h"

struct address_space {
	uint64_t root_physical;
	struct address_space *next;
};

#define VMM_WRITE 0x01U
#define VMM_USER 0x02U
#define VMM_EXEC 0x04U
#define VMM_UNCACHED 0x08U

bool vmm_init(const struct boot_info *info);
bool vmm_map_page(struct address_space *space, uint64_t virtual_address,
	uint64_t physical_address, unsigned int flags);
bool vmm_unmap_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address);
bool address_space_create(struct address_space *space);
bool address_space_activate(const struct address_space *space);
bool address_space_destroy(struct address_space *space);
struct address_space *vmm_active_address_space(void);
uint64_t vmm_hhdm_offset(void);
bool vmm_copy_from_user(const struct address_space *space, void *destination,
	uint64_t source, size_t length);
struct address_space *vmm_kernel_address_space(void);
uint64_t vmm_heap_start(void);
uint64_t vmm_heap_end(void);
uint64_t vmm_vmalloc_start(void);
uint64_t vmm_vmalloc_end(void);

#endif
