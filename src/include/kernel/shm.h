#ifndef KERNEL_SHM_H
#define KERNEL_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHM_PAGE_SIZE 4096ULL
#define SHM_ARENA_START 0x0000700000000000ULL
#define SHM_ARENA_END 0x00007fffffffc000ULL

#ifdef KERNEL_TEST_SHM_LIFECYCLE
uint64_t shm_test_live_pages(void);
#endif

struct shm_object {
	uint64_t references;
	size_t page_count;
	uint64_t pages[];
};

struct shm_mapping {
	uint64_t base;
	struct shm_object *object;
	struct shm_mapping *next;
};

struct shm_mapping_set {
	struct shm_mapping *head;
};

typedef bool (*shm_map_page_callback)(void *context,
	uint64_t virtual_address, uint64_t physical_address);
typedef bool (*shm_unmap_page_callback)(void *context,
	uint64_t virtual_address, uint64_t *physical_address);

/* map_page returning false means that page was not installed. */

/* A successful create returns one caller-owned reference. */
bool shm_object_create(size_t page_count, struct shm_object **object);
bool shm_object_retain(struct shm_object *object);
bool shm_object_release(struct shm_object *object);

void shm_mapping_set_init(struct shm_mapping_set *set);
/* On success, attach_owned transfers the caller's existing reference. */
bool shm_mapping_attach_owned(struct shm_mapping_set *set, uint64_t base,
	struct shm_object *object);
bool shm_mapping_attach_reference(struct shm_mapping_set *set, uint64_t base,
	struct shm_object *object);
bool shm_mapping_detach(struct shm_mapping_set *set, uint64_t base,
	shm_unmap_page_callback unmap_page, void *context);
/* Destroy only after page tables no longer contain the set's mappings. */
void shm_mapping_set_destroy(struct shm_mapping_set *set);
/* Clone requires an empty destination; failed clones leave it empty. */
bool shm_mapping_set_clone(const struct shm_mapping_set *source,
	struct shm_mapping_set *destination,
	shm_map_page_callback map_page,
	shm_unmap_page_callback unmap_page, void *context);

#endif
