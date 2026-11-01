#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/acpi_mmio_range.h"

#define PAGE 4096ULL

static struct acpi_resource_range
valid_resource(uint64_t base, uint64_t length)
{
	return (struct acpi_resource_range){
		.base = base,
		.length = length,
		.kind = ACPI_RESOURCE_KIND_MEMORY,
		.attributes = ACPI_RESOURCE_ATTR_MMIO_UNCACHED_NONPREFETCHABLE
	};
}

static void
test_valid_containment_and_exact_end(void)
{
	struct acpi_resource_range resource = valid_resource(0x10000, PAGE * 2);
	struct acpi_physical_range request = {0x10800, 0x100};
	struct acpi_physical_range extent = {0xdead, 0xbeef};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4,
	    &extent) == ACPI_RANGE_OK);
	assert(extent.base == 0x10000 && extent.length == PAGE);
	request = (struct acpi_physical_range){0x11000, PAGE};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4,
	    &extent) == ACPI_RANGE_OK);
	assert(extent.base == 0x11000 && extent.length == PAGE);
}

static void
test_invalid_resource_and_attributes(void)
{
	struct acpi_resource_range resource = valid_resource(0x10000, PAGE);
	struct acpi_physical_range request = {0x10000, 4};
	struct acpi_physical_range out = {0x1234, 0x5678};
	struct acpi_physical_range saved = out;
	assert(acpi_mmio_range_calculate(NULL, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_INVALID_ARGUMENT);
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, NULL) ==
	    ACPI_RANGE_INVALID_ARGUMENT);
	resource.kind = ACPI_RESOURCE_KIND_IO;
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_INVALID_RESOURCE);
	resource = valid_resource(0x10000, PAGE);
	resource.attributes = ACPI_RESOURCE_ATTR_UNSUPPORTED;
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_INVALID_RESOURCE);
	resource = valid_resource(0x10000, 0);
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_INVALID_RESOURCE);
	resource = valid_resource(UINT64_MAX - 7U, 16);
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_OVERFLOW);
	assert(out.base == saved.base && out.length == saved.length);
}

static void
test_request_bounds_and_overflow(void)
{
	struct acpi_resource_range resource = valid_resource(0x10000, PAGE * 2);
	struct acpi_physical_range request = {0x0fff0, 4};
	struct acpi_physical_range out = {7, 9};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_NOT_CONTAINED);
	request = (struct acpi_physical_range){0x12000, 4};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_NOT_CONTAINED);
	request = (struct acpi_physical_range){0x10000, 0};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_INVALID_REQUEST);
	request = (struct acpi_physical_range){UINT64_MAX - 3U, 8};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_OVERFLOW);
	assert(out.base == 7 && out.length == 9);
}

static void
test_alignment_and_page_extent(void)
{
	struct acpi_resource_range resource = valid_resource(0x10000, PAGE);
	struct acpi_physical_range request = {0x10002, 4};
	struct acpi_physical_range out = {1, 2};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_INVALID_REQUEST);
	request = (struct acpi_physical_range){0x10004, 4};
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 3, &out) ==
	    ACPI_RANGE_INVALID_ALIGNMENT);
	assert(acpi_mmio_range_calculate(&resource, &request, 3000, 4, &out) ==
	    ACPI_RANGE_INVALID_ALIGNMENT);
	request = (struct acpi_physical_range){0x10004, 4};
	resource = valid_resource(0x10004, PAGE);
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 4, &out) ==
	    ACPI_RANGE_UNMAPPABLE_EXTENT);
	request = (struct acpi_physical_range){UINT64_MAX - 7U, 4};
	resource = valid_resource(UINT64_MAX - PAGE, PAGE);
	assert(acpi_mmio_range_calculate(&resource, &request, PAGE, 1, &out) ==
	    ACPI_RANGE_OVERFLOW);
}

int
main(void)
{
	test_valid_containment_and_exact_end();
	test_invalid_resource_and_attributes();
	test_request_bounds_and_overflow();
	test_alignment_and_page_extent();
	puts("ACPI MMIO resource range validation tests passed");
	return 0;
}
