#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/boot_entropy.h"

#define TEST_MAP_COUNT 3U

struct fixture {
	struct limine_entropy_response response;
	uint64_t values[BOOT_ENTROPY_MAX_VALUES];
	struct limine_memmap_entry entries[TEST_MAP_COUNT];
	struct limine_memmap_entry *entry_pointers[TEST_MAP_COUNT];
	struct limine_memmap_response memmap;
};

static struct limine_memmap_entry
entry_for(const void *pointer, size_t length, uint64_t type)
{
	return (struct limine_memmap_entry){
		.base = (uintptr_t)pointer,
		.length = length,
		.type = type
	};
}

static void
fixture_init(struct fixture *fixture)
{
	fixture->values[0] = UINT64_C(0x123456789abcdef0);
	fixture->values[1] = UINT64_C(0xfedcba9876543210);
	fixture->values[2] = 3;
	fixture->values[3] = 4;
	fixture->response = (struct limine_entropy_response){
		.revision = 0,
		.value_count = BOOT_ENTROPY_MAX_VALUES,
		.values = fixture->values
	};
	fixture->entries[0] = entry_for(&fixture->response,
	    sizeof(fixture->response), LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);
	fixture->entries[1] = entry_for(fixture->values,
	    sizeof(fixture->values), LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);
	fixture->entries[2] = entry_for(fixture->entries,
	    sizeof(fixture->entries), LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);
	for (size_t i = 0; i < TEST_MAP_COUNT; i++)
		fixture->entry_pointers[i] = &fixture->entries[i];
	fixture->memmap = (struct limine_memmap_response){
		.entry_count = TEST_MAP_COUNT,
		.entries = fixture->entry_pointers
	};
}

static void
assert_cleared(const uint64_t output[BOOT_ENTROPY_MAX_VALUES], size_t count)
{
	assert(count == 0);
	for (size_t i = 0; i < BOOT_ENTROPY_MAX_VALUES; i++)
		assert(output[i] == 0);
}

int
main(void)
{
	struct fixture fixture;
	uint64_t output[BOOT_ENTROPY_MAX_VALUES];
	size_t count;

	fixture_init(&fixture);
	for (size_t i = 0; i < BOOT_ENTROPY_MAX_VALUES; i++)
		output[i] = UINT64_MAX;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_AVAILABLE_UNTRUSTED);
	assert(count == BOOT_ENTROPY_MAX_VALUES);
	assert(output[0] == fixture.values[0] &&
	    output[3] == fixture.values[3]);

	fixture.response.value_count = 2;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_PARTIAL_UNTRUSTED);
	assert(count == 2 && output[1] == fixture.values[1] && output[2] == 0);

	fixture.response.value_count = 5;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);

	fixture.response.value_count = 1;
	fixture.response.revision = 1;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);

	fixture.response.revision = 0;
	fixture.response.values = NULL;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);

	fixture.response.value_count = 0;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_EMPTY);
	assert_cleared(output, count);

	assert(boot_entropy_inspect(NULL, 0, &fixture.memmap, output,
	    &count) == BOOT_ENTROPY_MISSING);
	assert_cleared(output, count);
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    NULL, &count) == BOOT_ENTROPY_INVALID);
	assert(count == 0);
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, NULL) == BOOT_ENTROPY_INVALID);
	for (size_t i = 0; i < BOOT_ENTROPY_MAX_VALUES; i++)
		assert(output[i] == 0);

	fixture.response.value_count = 1;
	fixture.response.values = fixture.values;
	fixture.entries[1].type = LIMINE_MEMMAP_USABLE;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);

	fixture.entries[1].type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE;
	assert(boot_entropy_inspect(&fixture.response, UINT64_MAX,
	    &fixture.memmap, output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);

	fixture.memmap.entry_count = 513;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);

	fixture.response.values = (uint64_t *)(uintptr_t)UINTPTR_MAX;
	fixture.memmap.entry_count = TEST_MAP_COUNT;
	assert(boot_entropy_inspect(&fixture.response, 0, &fixture.memmap,
	    output, &count) == BOOT_ENTROPY_INVALID);
	assert_cleared(output, count);
	puts("Boot entropy inspection tests: bounds, revision, HHDM, ownership and fail-closed copy passed");
	return 0;
}
