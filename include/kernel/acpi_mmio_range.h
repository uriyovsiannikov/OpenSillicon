#ifndef KERNEL_ACPI_MMIO_RANGE_H
#define KERNEL_ACPI_MMIO_RANGE_H

#include <stdbool.h>
#include <stdint.h>

/* Canonicalized resource facts, not a raw AML/ACPICA descriptor. A future
 * discovery layer must establish these facts from an evaluated _CRS result.
 */
enum acpi_resource_kind {
	ACPI_RESOURCE_KIND_INVALID = 0,
	ACPI_RESOURCE_KIND_MEMORY,
	ACPI_RESOURCE_KIND_IO
};

enum acpi_resource_attributes {
	ACPI_RESOURCE_ATTR_UNSUPPORTED = 0,
	ACPI_RESOURCE_ATTR_MMIO_UNCACHED_NONPREFETCHABLE
};

struct acpi_resource_range {
	uint64_t base;
	uint64_t length;
	enum acpi_resource_kind kind;
	enum acpi_resource_attributes attributes;
};

struct acpi_physical_range {
	uint64_t base;
	uint64_t length;
};

enum acpi_range_status {
	ACPI_RANGE_OK = 0,
	ACPI_RANGE_INVALID_ARGUMENT,
	ACPI_RANGE_INVALID_RESOURCE,
	ACPI_RANGE_INVALID_REQUEST,
	ACPI_RANGE_OVERFLOW,
	ACPI_RANGE_NOT_CONTAINED,
	ACPI_RANGE_INVALID_ALIGNMENT,
	ACPI_RANGE_UNMAPPABLE_EXTENT
};

/* Pure arithmetic only. Success validates containment and the page-rounded
 * extent against a caller-supplied canonical descriptor. It does NOT prove
 * descriptor provenance, device ownership, VMM safety, or authorize mapping.
 * On failure, extent_out is unchanged.
 */
enum acpi_range_status acpi_mmio_range_calculate(
	const struct acpi_resource_range *resource,
	const struct acpi_physical_range *requested, uint64_t page_size,
	uint64_t required_alignment, struct acpi_physical_range *extent_out);

#endif
