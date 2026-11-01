#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/x86_64/acpi.h"
#include "kernel/vmm.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/timer.h"
#include "kernel/vmm.h"
#include "acpi.h"
#include "acpiosxf.h"
#include "acpixf.h"

static void
write16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void
write32(uint8_t *bytes, uint32_t value)
{
	for (unsigned int i = 0; i < 4; i++)
		bytes[i] = (uint8_t)(value >> (i * 8));
}

static void
write64(uint8_t *bytes, uint64_t value)
{
	write32(bytes, (uint32_t)value);
	write32(bytes + 4, (uint32_t)(value >> 32));
}

static void
set_checksum(uint8_t *table, size_t length)
{
	uint8_t sum = 0;

	table[9] = 0;
	for (size_t i = 0; i < length; i++)
		sum = (uint8_t)(sum + table[i]);
	table[9] = (uint8_t)(0U - sum);
}

static size_t
make_madt(uint8_t *table)
{
	size_t length = 44;

	memset(table, 0, 256);
	memcpy(table, "APIC", 4);
	write32(table + 4, 44);
	table[8] = 1;
	write32(table + 36, 0xfee00000U);
	write32(table + 40, 1);
	set_checksum(table, length);
	return length;
}

static void
add_record(uint8_t *table, size_t *length, uint8_t type, uint8_t size)
{
	uint8_t *record = table + *length;

	memset(record, 0, size);
	record[0] = type;
	record[1] = size;
	*length += size;
	write32(table + 4, (uint32_t)*length);
	set_checksum(table, *length);
}

static void
test_madt(void)
{
	uint8_t table[256];
	struct acpi_madt_info info;
	size_t length = make_madt(table);

	add_record(table, &length, 1, 12);
	table[46] = 2;
	write32(table + 48, 0xfec00000U);
	write32(table + 52, 0);
	set_checksum(table, length);
	add_record(table, &length, 2, 10);
	write32(table + 60, 2);
	write16(table + 64, 0x0f);
	set_checksum(table, length);
	add_record(table, &length, 3, 8);
	write16(table + 68, 0x0f);
	write32(table + 70, 9);
	set_checksum(table, length);
	add_record(table, &length, 4, 6);
	set_checksum(table, length);
	assert(acpi_parse_madt(table, length, &info) == 0);
	assert(info.lapic_address == 0xfee00000U && info.flags == 1);
	assert(info.ioapic_count == 1 && info.ioapics[0].id == 2);
	assert(info.ioapics[0].address == 0xfec00000U &&
	    info.ioapics[0].gsi_base == 0);
	assert(info.override_count == 1 && info.overrides[0].gsi == 2 &&
	    info.overrides[0].flags == 0x0f);
	assert(info.nmi_count == 1 && info.nmis[0].gsi == 9);

	table[0] = 'X';
	assert(acpi_parse_madt(table, length, &info) == -22);
	table[0] = 'A';
	assert(acpi_parse_madt(table, length - 1, &info) == -22);
	table[length - 1] ^= 1;
	assert(acpi_parse_madt(table, length, &info) == -22);

	length = make_madt(table);
	add_record(table, &length, 1, 1);
	assert(acpi_parse_madt(table, length, &info) == -22);
	length = make_madt(table);
	add_record(table, &length, 1, 12);
	table[45] = 11;
	set_checksum(table, length);
	assert(acpi_parse_madt(table, length, &info) == -22);
	length = make_madt(table);
	add_record(table, &length, 9, 16);
	assert(acpi_parse_madt(table, length, &info) == -22);
	assert(acpi_parse_madt(NULL, length, &info) == -22);
}

static size_t
make_mcfg(uint8_t *table)
{
	const size_t length = 60;

	memset(table, 0, 128);
	memcpy(table, "MCFG", 4);
	write32(table + 4, (uint32_t)length);
	table[8] = 1;
	write64(table + 44, 0xe0000000ULL);
	write16(table + 52, 0);
	table[54] = 0;
	table[55] = 255;
	set_checksum(table, length);
	return length;
}

static void
test_mcfg(void)
{
	uint8_t table[128];
	struct acpi_mcfg_region regions[4];
	size_t count = 0;
	size_t length = make_mcfg(table);

	assert(acpi_parse_mcfg(table, length, regions, 4, &count) == 0);
	assert(count == 1 && regions[0].base == 0xe0000000ULL);
	assert(regions[0].start_bus == 0 && regions[0].end_bus == 255);
	table[54] = 1;
	table[55] = 0;
	set_checksum(table, length);
	assert(acpi_parse_mcfg(table, length, regions, 4, &count) == -22);
	length = make_mcfg(table);
	write32(table + 4, 59);
	set_checksum(table, length);
	assert(acpi_parse_mcfg(table, length, regions, 4, &count) == -22);
	length = make_mcfg(table);
	write64(table + 44, 0xe0001000ULL);
	set_checksum(table, length);
	assert(acpi_parse_mcfg(table, length, regions, 4, &count) == -22);
	length = make_mcfg(table);
	write32(table + 56, 1);
	set_checksum(table, length);
	assert(acpi_parse_mcfg(table, length, regions, 4, &count) == -22);
	length = make_mcfg(table);
	assert(acpi_parse_mcfg(table, length - 1, regions, 4, &count) == -22);
}

ACPI_STATUS AcpiInitializeSubsystem(void) { return AE_OK; }
ACPI_STATUS AcpiInitializeTables(ACPI_TABLE_DESC *storage, UINT32 count,
	BOOLEAN resize) { (void)storage; (void)count; (void)resize; return AE_OK; }
ACPI_STATUS AcpiLoadTables(void) { return AE_OK; }
ACPI_STATUS AcpiGetTable(char *signature, UINT32 instance,
	ACPI_TABLE_HEADER **table) { (void)signature; (void)instance; (void)table; return AE_NOT_FOUND; }
void *kheap_alloc(size_t size) { (void)size; return NULL; }
bool kheap_free(void *allocation) { (void)allocation; return false; }
void klog(const char *message) { (void)message; }
uint64_t timer_ticks(void) { return 0; }
uint64_t vmm_hhdm_offset(void) { return 0; }
uint64_t vmm_vmalloc_start(void) { return 0; }
uint64_t vmm_vmalloc_end(void) { return UINT64_MAX; }
/* Parser-only tests never map devices; fail closed if that path is reached. */
uint64_t vmm_vmalloc_device_start(void) { return 0; }
struct address_space *vmm_kernel_address_space(void) { return NULL; }
bool vmm_map_page(struct address_space *space, uint64_t va, uint64_t pa,
	unsigned int flags) { (void)space; (void)va; (void)pa; (void)flags; return false; }
bool vmm_unmap_page(struct address_space *space, uint64_t va, uint64_t *pa)
{ (void)space; (void)va; (void)pa; return false; }

int
main(void)
{
	test_madt();
	test_mcfg();
	puts("ACPI MADT/MCFG bounds, checksums, and malformed-table tests passed");
	return 0;
}
