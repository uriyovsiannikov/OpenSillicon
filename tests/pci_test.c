#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arch/x86_64/pci.h"
#include "kernel/vmm.h"

struct fake_function {
	uint8_t bus;
	uint8_t device;
	uint8_t function;
	uint32_t config[64];
};

struct fake_bus {
	struct fake_function functions[4];
	size_t count;
};

struct result_set {
	struct pci_device_info devices[8];
	size_t count;
};

static struct fake_function *
add_function(struct fake_bus *bus, uint8_t address, uint8_t function,
	uint16_t vendor, uint16_t device)
{
	struct fake_function *entry = &bus->functions[bus->count++];

	memset(entry, 0, sizeof(*entry));
	entry->bus = address;
	entry->device = function;
	entry->function = 0;
	entry->config[0] = ((uint32_t)device << 16) | vendor;
	return entry;
}

static bool
fake_read(void *context, uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, uint32_t *value)
{
	struct fake_bus *fake = context;

	if (segment != 0 || (offset & 3U) != 0 || offset >= 256)
		return false;
	for (size_t i = 0; i < fake->count; i++) {
		struct fake_function *entry = &fake->functions[i];
		if (entry->bus == bus && entry->device == device &&
		    entry->function == function) {
			*value = entry->config[offset / 4];
			return true;
		}
	}
	*value = UINT32_MAX;
	return true;
}

static bool
record_device(const struct pci_device_info *device, void *context)
{
	struct result_set *results = context;

	assert(results->count < 8);
	results->devices[results->count++] = *device;
	return true;
}

static void
setup_topology(struct fake_bus *bus)
{
	struct fake_function *bridge;
	struct fake_function *multi0;
	struct fake_function *multi1;
	struct fake_function *ahci;

	memset(bus, 0, sizeof(*bus));
	bridge = add_function(bus, 0, 0, 0x8086, 0x1234);
	bridge->config[2] = 0x06040000U;
	bridge->config[3] = 0x00010000U;
	bridge->config[6] = 0x00010100U;
	multi0 = add_function(bus, 0, 1, 0x1234, 0x1111);
	multi0->config[3] = 0x00800000U;
	multi1 = add_function(bus, 0, 1, 0x1234, 0x2222);
	multi1->function = 1;
	ahci = add_function(bus, 1, 0, 0x8086, 0x2922);
	ahci->config[2] = 0x01060101U;
	ahci->config[9] = 0xfebf0000U;
}

static bool
has_device(const struct result_set *results, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t vendor, uint16_t id)
{
	for (size_t i = 0; i < results->count; i++) {
		const struct pci_device_info *entry = &results->devices[i];
		if (entry->bus == bus && entry->device == device &&
		    entry->function == function && entry->vendor_id == vendor &&
		    entry->device_id == id)
			return true;
	}
	return false;
}

static void
test_mcfg_scan(void)
{
	struct fake_bus bus;
	struct result_set results = {0};
	struct acpi_mcfg_region region = {
		.base = 0xe0000000,
		.segment = 0,
		.start_bus = 0,
		.end_bus = 1
	};

	setup_topology(&bus);
	assert(pci_enumerate_with_reader(fake_read, &bus, &region, 1,
	    record_device, &results));
	assert(results.count == 4);
	assert(has_device(&results, 0, 0, 0, 0x8086, 0x1234));
	assert(has_device(&results, 0, 1, 0, 0x1234, 0x1111));
	assert(has_device(&results, 0, 1, 1, 0x1234, 0x2222));
	assert(has_device(&results, 1, 0, 0, 0x8086, 0x2922));
	for (size_t i = 0; i < results.count; i++) {
		const struct pci_device_info *entry = &results.devices[i];
		if (entry->vendor_id == 0x8086 && entry->device_id == 0x2922) {
			assert(entry->class_code == 1 && entry->subclass == 6 &&
			    entry->programming_interface == 1);
			assert(entry->bars[5] == 0xfebf0000U);
		}
	}
}

static void
test_bridge_fallback_and_cycle(void)
{
	struct fake_bus bus;
	struct result_set results = {0};

	setup_topology(&bus);
	assert(pci_enumerate_with_reader(fake_read, &bus, NULL, 0,
	    record_device, &results));
	assert(results.count == 4);
	assert(has_device(&results, 1, 0, 0, 0x8086, 0x2922));
	bus.functions[0].config[6] = 0;
	memset(&results, 0, sizeof(results));
	assert(pci_enumerate_with_reader(fake_read, &bus, NULL, 0,
	    record_device, &results));
	assert(results.count == 3);
}

struct acpi_mcfg_region *unused_regions;
size_t acpi_mcfg_regions(struct acpi_mcfg_region *regions, size_t capacity)
{ (void)regions; (void)capacity; return 0; }
uint64_t vmm_vmalloc_start(void) { return 0; }
uint64_t vmm_vmalloc_end(void) { return UINT64_MAX; }
struct address_space *vmm_kernel_address_space(void) { return NULL; }
bool vmm_map_page(struct address_space *space, uint64_t va, uint64_t pa,
	unsigned int flags) { (void)space; (void)va; (void)pa; (void)flags; return false; }
bool vmm_unmap_page(struct address_space *space, uint64_t va, uint64_t *pa)
{ (void)space; (void)va; (void)pa; return false; }

int
main(void)
{
	test_mcfg_scan();
	test_bridge_fallback_and_cycle();
	puts("PCI MCFG enumeration, bridge traversal, multifunction, and AHCI tests passed");
	return 0;
}
