#ifndef ARCH_X86_64_ACPI_H
#define ARCH_X86_64_ACPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACPI_MADT_MAX_IOAPICS 8U
#define ACPI_MADT_MAX_OVERRIDES 32U
#define ACPI_MADT_MAX_NMIS 32U
#define ACPI_MCFG_MAX_REGIONS 16U

struct acpi_madt_ioapic {
	uint8_t id;
	uint32_t address;
	uint32_t gsi_base;
};

struct acpi_madt_override {
	uint8_t source;
	uint32_t gsi;
	uint16_t flags;
};

struct acpi_madt_nmi {
	uint32_t gsi;
	uint16_t flags;
};

struct acpi_madt_info {
	uint64_t lapic_address;
	uint32_t flags;
	size_t ioapic_count;
	size_t override_count;
	size_t nmi_count;
	struct acpi_madt_ioapic ioapics[ACPI_MADT_MAX_IOAPICS];
	struct acpi_madt_override overrides[ACPI_MADT_MAX_OVERRIDES];
	struct acpi_madt_nmi nmis[ACPI_MADT_MAX_NMIS];
};

struct acpi_mcfg_region {
	uint64_t base;
	uint16_t segment;
	uint8_t start_bus;
	uint8_t end_bus;
};

int acpi_parse_madt(const void *table, size_t table_size,
	struct acpi_madt_info *info);
int acpi_parse_mcfg(const void *table, size_t table_size,
	struct acpi_mcfg_region *regions, size_t capacity, size_t *count);
size_t acpi_mcfg_regions(struct acpi_mcfg_region *regions, size_t capacity);
bool acpi_init(uint64_t rsdp_physical, struct acpi_madt_info *info);

#endif
