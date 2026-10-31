#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/shm.h"
#include "kernel/vmm.h"

#define TEST_MAP_CAPACITY 32U
#define TEST_ALLOC_CAPACITY 64U

struct test_page {
	uint64_t address;
	bool allocated;
};

struct test_mapping {
	uint64_t virtual_address;
	uint64_t physical_address;
};

struct test_space {
	struct test_mapping mappings[TEST_MAP_CAPACITY];
	size_t mapping_count;
	unsigned int map_calls;
	unsigned int fail_map_call;
	unsigned int unmap_calls;
};

static void *heap_allocations[TEST_ALLOC_CAPACITY];
static size_t heap_allocation_count;
static unsigned int heap_alloc_calls;
static unsigned int fail_heap_alloc_call;
static struct test_page physical_pages[TEST_ALLOC_CAPACITY];
static size_t physical_page_count;
static unsigned int physical_alloc_calls;
static unsigned int fail_physical_alloc_call;
static unsigned int physical_free_calls;

void *
kheap_alloc(size_t size)
{
	void *allocation;

	heap_alloc_calls++;
	if (fail_heap_alloc_call != 0 &&
	    heap_alloc_calls == fail_heap_alloc_call)
		return NULL;
	assert(heap_allocation_count < TEST_ALLOC_CAPACITY);
	allocation = calloc(1, size);
	if (allocation != NULL)
		heap_allocations[heap_allocation_count++] = allocation;
	return allocation;
}

bool
kheap_free(void *allocation)
{
	size_t i;

	if (allocation == NULL)
		return false;
	for (i = 0; i < heap_allocation_count; i++) {
		if (heap_allocations[i] == allocation) {
			free(allocation);
			heap_allocations[i] = heap_allocations[--heap_allocation_count];
			return true;
		}
	}
	return false;
}

bool
pmm_alloc_page(uint64_t *physical_address)
{
	void *page;

	physical_alloc_calls++;
	if (physical_address == NULL || (fail_physical_alloc_call != 0 &&
	    physical_alloc_calls == fail_physical_alloc_call))
		return false;
	assert(physical_page_count < TEST_ALLOC_CAPACITY);
	page = aligned_alloc(SHM_PAGE_SIZE, SHM_PAGE_SIZE);
	if (page == NULL)
		return false;
	physical_pages[physical_page_count++] = (struct test_page){
		.address = (uintptr_t)page,
		.allocated = true
	};
	*physical_address = (uintptr_t)page;
	return true;
}

bool
pmm_free_page(uint64_t physical_address)
{
	size_t i;

	for (i = 0; i < physical_page_count; i++) {
		if (physical_pages[i].address == physical_address &&
		    physical_pages[i].allocated) {
			free((void *)(uintptr_t)physical_address);
			physical_pages[i].allocated = false;
			physical_free_calls++;
			return true;
		}
	}
	return false;
}

uint64_t
vmm_hhdm_offset(void)
{
	return 0;
}

void
klog(const char *message)
{
	(void)message;
}

_Noreturn void
kernel_halt(void)
{
	abort();
}

static size_t
live_physical_pages(void)
{
	size_t count = 0;
	size_t i;

	for (i = 0; i < physical_page_count; i++)
		if (physical_pages[i].allocated)
			count++;
	return count;
}

static bool
test_map_page(void *context, uint64_t virtual_address,
	uint64_t physical_address)
{
	struct test_space *space = context;
	size_t i;

	space->map_calls++;
	if (space->fail_map_call != 0 &&
	    space->map_calls == space->fail_map_call)
		return false;
	if (space->mapping_count == TEST_MAP_CAPACITY)
		return false;
	for (i = 0; i < space->mapping_count; i++)
		if (space->mappings[i].virtual_address == virtual_address)
			return false;
	space->mappings[space->mapping_count++] = (struct test_mapping){
		.virtual_address = virtual_address,
		.physical_address = physical_address
	};
	return true;
}

static bool
test_unmap_page(void *context, uint64_t virtual_address,
	uint64_t *physical_address)
{
	struct test_space *space = context;
	size_t i;

	space->unmap_calls++;
	for (i = 0; i < space->mapping_count; i++) {
		if (space->mappings[i].virtual_address == virtual_address) {
			*physical_address = space->mappings[i].physical_address;
			space->mappings[i] = space->mappings[--space->mapping_count];
			return true;
		}
	}
	return false;
}

static uint64_t
physical_at(const struct test_space *space, uint64_t virtual_address)
{
	size_t i;

	for (i = 0; i < space->mapping_count; i++)
		if (space->mappings[i].virtual_address == virtual_address)
			return space->mappings[i].physical_address;
	return 0;
}

static void
unmap_set(struct shm_mapping_set *set, struct test_space *space)
{
	struct shm_mapping *mapping;

	for (mapping = set->head; mapping != NULL; mapping = mapping->next) {
		size_t i;

		for (i = 0; i < mapping->object->page_count; i++) {
			uint64_t physical;

			assert(test_unmap_page(space,
			    mapping->base + i * SHM_PAGE_SIZE, &physical));
			assert(physical == mapping->object->pages[i]);
		}
	}
}

static void
assert_no_test_allocations(void)
{
	assert(heap_allocation_count == 0);
	assert(live_physical_pages() == 0);
}

static void
reset_test_failures(void)
{
	fail_heap_alloc_call = 0;
	fail_physical_alloc_call = 0;
}

static void
test_object_references_and_overflow(void)
{
	struct shm_object *object;
	unsigned int frees_before;
	size_t i;

	assert(shm_object_create(3, &object));
	assert(object->references == 1 && object->page_count == 3);
	assert(live_physical_pages() == 3);
	for (i = 0; i < object->page_count; i++) {
		const uint8_t *page = (const uint8_t *)(uintptr_t)object->pages[i];
		size_t byte;

		for (byte = 0; byte < SHM_PAGE_SIZE; byte++)
			assert(page[byte] == 0);
	}
	assert(shm_object_retain(object));
	assert(shm_object_retain(object));
	assert(object->references == 3);
	assert(shm_object_release(object) && object->references == 2);
	assert(shm_object_release(object) && object->references == 1);
	frees_before = physical_free_calls;
	assert(shm_object_release(object));
	assert(physical_free_calls == frees_before + 3);
	assert_no_test_allocations();

	assert(shm_object_create(1, &object));
	object->references = UINT64_MAX;
	assert(!shm_object_retain(object));
	object->references = 1;
	assert(shm_object_release(object));
	assert_no_test_allocations();
	assert(!shm_object_release(NULL));
}

static void
test_object_allocation_rollback(void)
{
	struct shm_object *object = (struct shm_object *)(uintptr_t)1;
	size_t heap_before = heap_allocation_count;
	size_t pages_before = live_physical_pages();

	assert(!shm_object_create(0, &object));
	assert(object == NULL);
	fail_heap_alloc_call = heap_alloc_calls + 1;
	assert(!shm_object_create(2, &object));
	assert(object == NULL);
	assert(heap_allocation_count == heap_before);
	assert(live_physical_pages() == pages_before);
	reset_test_failures();

	physical_alloc_calls = 0;
	fail_physical_alloc_call = 3;
	assert(!shm_object_create(4, &object));
	assert(object == NULL);
	assert(heap_allocation_count == heap_before);
	assert(live_physical_pages() == pages_before);
	reset_test_failures();
}

static void
test_attach_detach_and_idempotent_teardown(void)
{
	struct shm_mapping_set set;
	struct shm_object *object;
	struct test_space space = {0};
	unsigned int frees_before;

	shm_mapping_set_init(&set);
	assert(shm_object_create(2, &object));
	assert(shm_mapping_attach_owned(&set, 0x10000, object));
	assert(object->references == 1);
	assert(!shm_mapping_attach_reference(&set, 0x10000, object));
	assert(!shm_mapping_attach_reference(&set, 0x11000, object));
	assert(object->references == 1);
	assert(test_map_page(&space, 0x10000, object->pages[0]));
	assert(test_map_page(&space, 0x11000, object->pages[1]));
	frees_before = physical_free_calls;
	assert(shm_mapping_detach(&set, 0x10000, test_unmap_page, &space));
	assert(set.head == NULL && space.mapping_count == 0);
	assert(physical_free_calls == frees_before + 2);
	assert(!shm_mapping_detach(&set, 0x10000, test_unmap_page, &space));
	assert(physical_free_calls == frees_before + 2);
	shm_mapping_set_destroy(&set);
	shm_mapping_set_destroy(&set);
	assert_no_test_allocations();

	shm_mapping_set_init(&set);
	assert(shm_object_create(1, &object));
	assert(shm_mapping_attach_owned(&set, 0x20000, object));
	assert(test_map_page(&space, 0x20000, object->pages[0]));
	unmap_set(&set, &space); /* Address-space page tables go away first. */
	frees_before = physical_free_calls;
	shm_mapping_set_destroy(&set);
	assert(set.head == NULL && physical_free_calls == frees_before + 1);
	shm_mapping_set_destroy(&set);
	assert(physical_free_calls == frees_before + 1);
	assert_no_test_allocations();
}

static void
test_fork_generations(void)
{
	struct shm_mapping_set parent;
	struct shm_mapping_set child;
	struct shm_mapping_set grandchild;
	struct shm_object *object;
	struct test_space parent_space = {0};
	struct test_space child_space = {0};
	struct test_space grandchild_space = {0};
	uint8_t *shared_bytes;

	shm_mapping_set_init(&parent);
	shm_mapping_set_init(&child);
	shm_mapping_set_init(&grandchild);
	assert(shm_object_create(1, &object));
	assert(shm_mapping_attach_owned(&parent, 0x30000, object));
	assert(test_map_page(&parent_space, 0x30000, object->pages[0]));
	assert(shm_mapping_set_clone(&parent, &child, test_map_page,
	    test_unmap_page, &child_space));
	assert(object->references == 2);
	assert(shm_mapping_set_clone(&child, &grandchild, test_map_page,
	    test_unmap_page, &grandchild_space));
	assert(object->references == 3);
	assert(physical_at(&parent_space, 0x30000) == object->pages[0]);
	assert(physical_at(&child_space, 0x30000) == object->pages[0]);
	assert(physical_at(&grandchild_space, 0x30000) == object->pages[0]);
	shared_bytes = (uint8_t *)(uintptr_t)object->pages[0];
	shared_bytes[17] = 0xa5;
	assert(((uint8_t *)(uintptr_t)physical_at(&child_space, 0x30000))[17] ==
	    0xa5);
	assert(((uint8_t *)(uintptr_t)physical_at(&grandchild_space,
	    0x30000))[17] == 0xa5);
	assert(shm_mapping_detach(&child, 0x30000, test_unmap_page,
	    &child_space));
	assert(object->references == 2);
	assert(physical_at(&parent_space, 0x30000) == object->pages[0]);
	unmap_set(&parent, &parent_space);
	shm_mapping_set_destroy(&parent);
	assert(object->references == 1);
	unmap_set(&grandchild, &grandchild_space);
	shm_mapping_set_destroy(&grandchild);
	shm_mapping_set_destroy(&child);
	assert_no_test_allocations();
}

static void
test_clone_map_failure_rolls_back_all_references(void)
{
	struct shm_mapping_set parent;
	struct shm_mapping_set child;
	struct shm_object *first;
	struct shm_object *second;
	struct test_space parent_space = {0};
	struct test_space child_space = { .fail_map_call = 3 };
	unsigned int unmaps_before;

	shm_mapping_set_init(&parent);
	shm_mapping_set_init(&child);
	assert(shm_object_create(1, &first));
	assert(shm_object_create(2, &second));
	assert(shm_mapping_attach_owned(&parent, 0x50000, second));
	assert(shm_mapping_attach_owned(&parent, 0x40000, first));
	assert(test_map_page(&parent_space, 0x40000, first->pages[0]));
	assert(test_map_page(&parent_space, 0x50000, second->pages[0]));
	assert(test_map_page(&parent_space, 0x51000, second->pages[1]));
	unmaps_before = child_space.unmap_calls;
	assert(!shm_mapping_set_clone(&parent, &child, test_map_page,
	    test_unmap_page, &child_space));
	assert(child.head == NULL && child_space.mapping_count == 0);
	assert(child_space.unmap_calls == unmaps_before + 2);
	assert(first->references == 1 && second->references == 1);
	assert(physical_at(&parent_space, 0x40000) == first->pages[0]);
	assert(physical_at(&parent_space, 0x50000) == second->pages[0]);
	unmap_set(&parent, &parent_space);
	shm_mapping_set_destroy(&parent);
	assert_no_test_allocations();
}

static void
test_clone_metadata_failure_rolls_back_prior_mapping(void)
{
	struct shm_mapping_set parent;
	struct shm_mapping_set child;
	struct shm_object *first;
	struct shm_object *second;
	struct test_space parent_space = {0};
	struct test_space child_space = {0};

	shm_mapping_set_init(&parent);
	shm_mapping_set_init(&child);
	assert(shm_object_create(1, &first));
	assert(shm_object_create(1, &second));
	assert(shm_mapping_attach_owned(&parent, 0x70000, second));
	assert(shm_mapping_attach_owned(&parent, 0x60000, first));
	assert(test_map_page(&parent_space, 0x60000, first->pages[0]));
	assert(test_map_page(&parent_space, 0x70000, second->pages[0]));
	/* The clone's second record allocation fails after its first region commits. */
	fail_heap_alloc_call = heap_alloc_calls + 2;
	assert(!shm_mapping_set_clone(&parent, &child, test_map_page,
	    test_unmap_page, &child_space));
	assert(child.head == NULL && child_space.mapping_count == 0);
	assert(first->references == 1 && second->references == 1);
	assert(physical_at(&parent_space, 0x60000) == first->pages[0]);
	assert(physical_at(&parent_space, 0x70000) == second->pages[0]);
	reset_test_failures();
	unmap_set(&parent, &parent_space);
	shm_mapping_set_destroy(&parent);
	assert_no_test_allocations();
}

static void
test_attach_metadata_failure_keeps_caller_reference(void)
{
	struct shm_mapping_set set;
	struct shm_object *object;

	shm_mapping_set_init(&set);
	assert(shm_object_create(1, &object));
	fail_heap_alloc_call = heap_alloc_calls + 1;
	assert(!shm_mapping_attach_owned(&set, 0x80000, object));
	assert(set.head == NULL && object->references == 1);
	reset_test_failures();
	assert(shm_object_release(object));
	assert_no_test_allocations();
}

int
main(void)
{
	test_object_references_and_overflow();
	test_object_allocation_rollback();
	test_attach_detach_and_idempotent_teardown();
	test_fork_generations();
	test_clone_map_failure_rolls_back_all_references();
	test_clone_metadata_failure_rolls_back_prior_mapping();
	test_attach_metadata_failure_keeps_caller_reference();
	assert_no_test_allocations();
	puts("shared-memory object and mapping accounting tests passed");
	return 0;
}
