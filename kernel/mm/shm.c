#include <stddef.h>
#include <stdint.h>

#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/shm.h"
#include "kernel/vmm.h"

#ifdef KERNEL_TEST_SHM_ALLOC_FAILURE
static bool fail_shared_allocation_once = true;
#endif

#ifdef KERNEL_TEST_SHM_LIFECYCLE
static uint64_t test_live_pages;

uint64_t
shm_test_live_pages(void)
{
	return test_live_pages;
}
#endif

static _Noreturn void
shm_invariant_failure(const char *message)
{
	klog(message);
	kernel_halt();
}

static bool
object_size(size_t page_count, size_t *size)
{
	if (page_count == 0 || page_count >
	    (SIZE_MAX - sizeof(struct shm_object)) / sizeof(uint64_t))
		return false;
	*size = sizeof(struct shm_object) + page_count * sizeof(uint64_t);
	return true;
}

static bool
zero_physical_page(uint64_t physical_address)
{
	uint64_t hhdm_offset = vmm_hhdm_offset();
	uint64_t virtual_address;
	volatile uint8_t *page;
	size_t i;

	if ((physical_address & (SHM_PAGE_SIZE - 1)) != 0 ||
	    hhdm_offset > UINT64_MAX - physical_address)
		return false;
	virtual_address = hhdm_offset + physical_address;
	if (SHM_PAGE_SIZE - 1 > UINTPTR_MAX - (uintptr_t)virtual_address)
		return false;
	page = (volatile uint8_t *)(uintptr_t)virtual_address;
	for (i = 0; i < SHM_PAGE_SIZE; i++)
		page[i] = 0;
	return true;
}

bool
shm_object_create(size_t page_count, struct shm_object **object_out)
{
	struct shm_object *object;
	size_t allocation_size;
	size_t allocated = 0;

	if (object_out == NULL)
		return false;
	*object_out = NULL;
	if (!object_size(page_count, &allocation_size))
		return false;
	object = kheap_alloc(allocation_size);
	if (object == NULL)
		return false;
	object->references = 1;
	object->page_count = page_count;
	while (allocated < page_count) {
#ifdef KERNEL_TEST_SHM_ALLOC_FAILURE
		if (fail_shared_allocation_once && allocated == 1) {
			fail_shared_allocation_once = false;
			klog("OpenSillicon test: injected shared backing allocation failure\n");
			goto fail;
		}
#endif
		if (!pmm_alloc_page(&object->pages[allocated]))
			goto fail;
		if (!zero_physical_page(object->pages[allocated])) {
			if (!pmm_free_page(object->pages[allocated]))
				shm_invariant_failure(
				    "OpenSillicon panic: failed to roll back shared page\n");
			goto fail;
		}
		allocated++;
	}
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	if ((uint64_t)page_count > UINT64_MAX - test_live_pages)
		shm_invariant_failure("OpenSillicon panic: shared test page count overflow\n");
	test_live_pages += (uint64_t)page_count;
#endif
	*object_out = object;
	return true;

fail:
	while (allocated != 0) {
		allocated--;
		if (!pmm_free_page(object->pages[allocated]))
			shm_invariant_failure(
			    "OpenSillicon panic: failed to roll back shared pages\n");
	}
	if (!kheap_free(object))
		shm_invariant_failure(
		    "OpenSillicon panic: failed to release shared metadata\n");
	return false;
}

bool
shm_object_retain(struct shm_object *object)
{
	if (object == NULL || object->references == 0 ||
	    object->references == UINT64_MAX)
		return false;
	object->references++;
	return true;
}

bool
shm_object_release(struct shm_object *object)
{
	size_t i;

	if (object == NULL || object->references == 0)
		return false;
	if (--object->references != 0)
		return true;
#ifdef KERNEL_TEST_SHM_LIFECYCLE
	if ((uint64_t)object->page_count > test_live_pages)
		shm_invariant_failure("OpenSillicon panic: shared test page count underflow\n");
	test_live_pages -= (uint64_t)object->page_count;
#endif
	for (i = 0; i < object->page_count; i++) {
		if (!pmm_free_page(object->pages[i]))
			shm_invariant_failure(
			    "OpenSillicon panic: failed to release shared page\n");
	}
	if (!kheap_free(object))
		shm_invariant_failure(
		    "OpenSillicon panic: failed to release shared metadata\n");
	return true;
}

static bool
mapping_range_valid(uint64_t base, const struct shm_object *object,
	uint64_t *end)
{
	uint64_t bytes;

	if (base == 0 || object == NULL || object->references == 0 ||
	    (base & (SHM_PAGE_SIZE - 1)) != 0 ||
	    object->page_count > UINT64_MAX / SHM_PAGE_SIZE)
		return false;
	bytes = (uint64_t)object->page_count * SHM_PAGE_SIZE;
	if (bytes == 0 || base > UINT64_MAX - bytes)
		return false;
	*end = base + bytes;
	return true;
}

static bool
mapping_base_available(const struct shm_mapping_set *set, uint64_t base,
	uint64_t end)
{
	const struct shm_mapping *mapping;

	for (mapping = set->head; mapping != NULL; mapping = mapping->next) {
		uint64_t other_end;

		if (!mapping_range_valid(mapping->base, mapping->object, &other_end) ||
		    (base < other_end && mapping->base < end))
			return false;
	}
	return true;
}

static struct shm_mapping *
mapping_allocate(struct shm_mapping_set *set, uint64_t base,
	struct shm_object *object)
{
	struct shm_mapping *mapping;
	uint64_t end;

	if (set == NULL || !mapping_range_valid(base, object, &end) ||
	    !mapping_base_available(set, base, end))
		return NULL;
	mapping = kheap_alloc(sizeof(*mapping));
	if (mapping == NULL)
		return NULL;
	mapping->base = base;
	mapping->object = object;
	mapping->next = NULL;
	return mapping;
}

static void
mapping_publish(struct shm_mapping_set *set, struct shm_mapping *mapping)
{
	mapping->next = set->head;
	set->head = mapping;
}

void
shm_mapping_set_init(struct shm_mapping_set *set)
{
	if (set != NULL)
		set->head = NULL;
}

bool
shm_mapping_attach_owned(struct shm_mapping_set *set, uint64_t base,
	struct shm_object *object)
{
	struct shm_mapping *mapping = mapping_allocate(set, base, object);

	if (mapping == NULL)
		return false;
	/* Success transfers the caller's existing object reference to this node. */
	mapping_publish(set, mapping);
	return true;
}

bool
shm_mapping_attach_reference(struct shm_mapping_set *set, uint64_t base,
	struct shm_object *object)
{
	struct shm_mapping *mapping = mapping_allocate(set, base, object);

	if (mapping == NULL)
		return false;
	if (!shm_object_retain(object)) {
		if (!kheap_free(mapping))
			shm_invariant_failure(
			    "OpenSillicon panic: failed to release shared mapping\n");
		return false;
	}
	mapping_publish(set, mapping);
	return true;
}

static void
unmap_mapping(const struct shm_mapping *mapping,
	shm_unmap_page_callback unmap_page, void *context,
	size_t page_count)
{
	size_t i = page_count;

	while (i != 0) {
		uint64_t address;
		uint64_t physical;

		i--;
		address = mapping->base + (uint64_t)i * SHM_PAGE_SIZE;
		if (!unmap_page(context, address, &physical) ||
		    physical != mapping->object->pages[i])
			shm_invariant_failure(
			    "OpenSillicon panic: shared mapping rollback failed\n");
	}
}

bool
shm_mapping_detach(struct shm_mapping_set *set, uint64_t base,
	shm_unmap_page_callback unmap_page, void *context)
{
	struct shm_mapping **link;
	struct shm_mapping *mapping;

	if (set == NULL || unmap_page == NULL)
		return false;
	link = &set->head;
	while (*link != NULL && (*link)->base != base)
		link = &(*link)->next;
	if (*link == NULL)
		return false;
	mapping = *link;
	unmap_mapping(mapping, unmap_page, context,
	    mapping->object->page_count);
	*link = mapping->next;
	if (!shm_object_release(mapping->object) || !kheap_free(mapping))
		shm_invariant_failure(
		    "OpenSillicon panic: failed to detach shared mapping\n");
	return true;
}

void
shm_mapping_set_destroy(struct shm_mapping_set *set)
{
	struct shm_mapping *mapping;

	if (set == NULL)
		return;
	/* The caller removes page-table mappings before dropping these references. */
	mapping = set->head;
	set->head = NULL;
	while (mapping != NULL) {
		struct shm_mapping *next = mapping->next;

		if (!shm_object_release(mapping->object) || !kheap_free(mapping))
			shm_invariant_failure(
			    "OpenSillicon panic: failed to destroy shared mapping\n");
		mapping = next;
	}
}

static void
rollback_clone(struct shm_mapping_set *destination,
	shm_unmap_page_callback unmap_page, void *context)
{
	struct shm_mapping *mapping;

	for (mapping = destination->head; mapping != NULL;
	    mapping = mapping->next)
		unmap_mapping(mapping, unmap_page, context,
		    mapping->object->page_count);
	shm_mapping_set_destroy(destination);
}

bool
shm_mapping_set_clone(const struct shm_mapping_set *source,
	struct shm_mapping_set *destination,
	shm_map_page_callback map_page,
	shm_unmap_page_callback unmap_page, void *context)
{
	const struct shm_mapping *source_mapping;
	struct shm_mapping **tail;

	if (source == NULL || destination == NULL || source == destination ||
	    destination->head != NULL || map_page == NULL || unmap_page == NULL)
		return false;
	tail = &destination->head;
	for (source_mapping = source->head; source_mapping != NULL;
	    source_mapping = source_mapping->next) {
		struct shm_mapping *child_mapping;
		size_t installed = 0;
		size_t i;

		child_mapping = mapping_allocate(destination,
		    source_mapping->base, source_mapping->object);
		if (child_mapping == NULL)
			goto fail;
		if (!shm_object_retain(source_mapping->object)) {
			if (!kheap_free(child_mapping))
				shm_invariant_failure(
				    "OpenSillicon panic: failed to release shared mapping\n");
			goto fail;
		}
		for (i = 0; i < source_mapping->object->page_count; i++) {
			uint64_t address = source_mapping->base +
			    (uint64_t)i * SHM_PAGE_SIZE;

			if (!map_page(context, address,
			    source_mapping->object->pages[i]))
				break;
			installed++;
		}
		if (installed != source_mapping->object->page_count) {
			unmap_mapping(child_mapping, unmap_page, context, installed);
			if (!shm_object_release(source_mapping->object) ||
			    !kheap_free(child_mapping))
				shm_invariant_failure(
				    "OpenSillicon panic: failed to roll back shared clone\n");
			goto fail;
		}
		*tail = child_mapping;
		tail = &child_mapping->next;
	}
	return true;

fail:
	rollback_clone(destination, unmap_page, context);
	return false;
}
