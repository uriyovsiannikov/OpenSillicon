#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/boot.h"
#include "kernel/shm.h"

struct address_space {
	uint64_t root_physical;
	uint64_t shm_next_address;
	struct shm_mapping_set shared_mappings;
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
bool vmm_map_shared_object(struct address_space *space, uint64_t base,
	struct shm_object *object);
bool vmm_unmap_shared_object(struct address_space *space, uint64_t base);
int vmm_shm_create(struct address_space *space, uint64_t size,
	uint64_t *base_out);
int vmm_shm_detach(struct address_space *space, uint64_t base);
#ifdef KERNEL_TEST_SHM_FORK_FAILURE
void vmm_test_arm_shm_fork_failure(void);
#endif
bool address_space_create(struct address_space *space);
bool address_space_activate(const struct address_space *space);
bool address_space_destroy(struct address_space *space);
struct address_space *vmm_active_address_space(void);
uint64_t vmm_hhdm_offset(void);
bool vmm_copy_from_user(const struct address_space *space, void *destination,
	uint64_t source, size_t length);
bool vmm_copy_to_user(const struct address_space *space, uint64_t destination,
	const void *source, size_t length);
bool vmm_validate_user_write(const struct address_space *space,
	uint64_t destination, size_t length);
bool vmm_validate_user_exec(const struct address_space *space,
	uint64_t address, size_t length);
bool address_space_clone(const struct address_space *source,
	struct address_space *destination);
bool address_space_destroy_with_pages(struct address_space *space);
struct address_space *vmm_kernel_address_space(void);
uint64_t vmm_heap_start(void);
uint64_t vmm_heap_end(void);
uint64_t vmm_vmalloc_start(void);
uint64_t vmm_vmalloc_end(void);

#endif
