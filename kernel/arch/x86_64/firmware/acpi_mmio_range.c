#include <stddef.h>

#include "kernel/acpi_mmio_range.h"

static bool
is_power_of_two(uint64_t value)
{
	return value != 0 && (value & (value - 1U)) == 0;
}

enum acpi_range_status
acpi_mmio_range_calculate(const struct acpi_resource_range *resource,
	const struct acpi_physical_range *requested, uint64_t page_size,
	uint64_t required_alignment, struct acpi_physical_range *extent_out)
{
	struct acpi_physical_range extent;
	uint64_t resource_end;
	uint64_t request_end;
	uint64_t rounded_end;
	uint64_t page_mask;

	if (resource == NULL || requested == NULL || extent_out == NULL)
		return ACPI_RANGE_INVALID_ARGUMENT;
	if (resource->kind != ACPI_RESOURCE_KIND_MEMORY ||
	    resource->attributes !=
	    ACPI_RESOURCE_ATTR_MMIO_UNCACHED_NONPREFETCHABLE ||
	    resource->length == 0)
		return ACPI_RANGE_INVALID_RESOURCE;
	if (resource->base > UINT64_MAX - resource->length)
		return ACPI_RANGE_OVERFLOW;
	if (!is_power_of_two(page_size) || !is_power_of_two(required_alignment))
		return ACPI_RANGE_INVALID_ALIGNMENT;
	if (requested->length == 0 ||
	    (requested->base & (required_alignment - 1U)) != 0)
		return ACPI_RANGE_INVALID_REQUEST;
	if (requested->base > UINT64_MAX - requested->length)
		return ACPI_RANGE_OVERFLOW;
	resource_end = resource->base + resource->length;
	request_end = requested->base + requested->length;
	if (requested->base < resource->base || request_end > resource_end)
		return ACPI_RANGE_NOT_CONTAINED;

	page_mask = page_size - 1U;
	extent.base = requested->base & ~page_mask;
	if (request_end > UINT64_MAX - page_mask)
		return ACPI_RANGE_OVERFLOW;
	rounded_end = (request_end + page_mask) & ~page_mask;
	if (extent.base < resource->base || rounded_end <= extent.base ||
	    rounded_end > resource_end)
		return ACPI_RANGE_UNMAPPABLE_EXTENT;
	extent.length = rounded_end - extent.base;
	*extent_out = extent;
	return ACPI_RANGE_OK;
}
