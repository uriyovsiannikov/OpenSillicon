#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/boot.h"
#include "kernel/shm.h"

struct private_mapping {
	uint64_t base;
	uint64_t requested_size;
	uint64_t mapped_size;
	size_t page_count;
	uint64_t *physical_pages;
	uint8_t *page_protections;
	struct private_mapping *next;
};

struct address_space {
	uint64_t root_physical;
	uint64_t shm_next_address;
	struct shm_mapping_set shared_mappings;
	uint64_t private_map_cursor;
	struct private_mapping *private_mappings;
	struct address_space *next;
};

#define PRIVATE_MAP_ARENA_START 0x0000600000000000ULL
#define PRIVATE_MAP_ARENA_END 0x0000700000000000ULL
#define PRIVATE_MAP_USABLE_START (PRIVATE_MAP_ARENA_START + 4096ULL)
#define PRIVATE_MAP_USABLE_END (PRIVATE_MAP_ARENA_END - 4096ULL)

/* The top 1 GiB of vmalloc is reserved for bounded PCI MMIO leases. */
#define VMM_DEVICE_WINDOW_SIZE (1ULL << 30)

#define VMM_WRITE 0x01U
#define VMM_USER 0x02U
#define VMM_EXEC 0x04U
#define VMM_UNCACHED 0x08U

#define VMM_PROT_NONE 0U
#define VMM_PROT_READ 0x01U
#define VMM_PROT_WRITE 0x02U
#define VMM_PROT_EXEC 0x04U

static inline bool
vmm_private_protection_valid(unsigned int protection)
{
	return protection == VMM_PROT_NONE || protection == VMM_PROT_READ ||
	    protection == VMM_PROT_WRITE ||
	    protection == (VMM_PROT_READ | VMM_PROT_WRITE);
}

static inline bool
vmm_private_protection_range(uint64_t base, uint64_t length,
	uint64_t *rounded_length, uint64_t *end_out)
{
	uint64_t rounded;

	if (rounded_length == NULL || end_out == NULL || length == 0 ||
	    (base & 4095U) != 0 || length > UINT64_MAX - 4095U)
		return false;
	rounded = (length + 4095U) & ~4095ULL;
	if (rounded > UINT64_MAX - base)
		return false;
	*rounded_length = rounded;
	*end_out = base + rounded;
	return true;
}

static inline bool
vmm_private_unmap_length_matches(uint64_t mapped_size, uint64_t length)
{
	uint64_t rounded;

	if (mapped_size == 0 || (mapped_size & 4095U) != 0 || length == 0 ||
	    length > UINT64_MAX - 4095U)
		return false;
	rounded = (length + 4095U) & ~4095ULL;
	return rounded == mapped_size;
}

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
int vmm_private_map(struct address_space *space, uint64_t size,
	uint64_t *base_out);
int vmm_private_map_protected(struct address_space *space, uint64_t size,
	unsigned int protection, uint64_t *base_out);
int vmm_private_unmap(struct address_space *space, uint64_t base,
	uint64_t requested_size);
int vmm_private_unmap_complete(struct address_space *space, uint64_t base,
	uint64_t length);
int vmm_private_protect(struct address_space *space, uint64_t base,
	uint64_t length, unsigned int protection);
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
void vmm_test_arm_private_map_failure(unsigned int mapped_pages);
void vmm_test_arm_private_fork_failure(unsigned int copied_pages);
void vmm_test_arm_private_metadata_failure(unsigned int allocation_number);
bool vmm_test_install_private_collision(struct address_space *space);
bool vmm_test_remove_private_collision(struct address_space *space);
#endif
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
bool vmm_validate_user_read(const struct address_space *space,
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
uint64_t vmm_vmalloc_device_start(void);
uint64_t vmm_vmalloc_device_end(void);
/* Accepts a range wholly covered by Limine RESERVED memory or wholly absent
 * from the memory map; all overlaps with described RAM/boot memory are denied. */
bool vmm_physical_range_mmio_safe(uint64_t physical_address, uint64_t length);

#endif
