#include <string.h>

#include "arch/x86_64/pci.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096ULL
#define ECAM_BUS_SIZE (1ULL << 20)
#define ECAM_VIRTUAL_OFFSET (1ULL << 20)
#define ECAM_WINDOW_PAGES (ECAM_BUS_SIZE / PAGE_SIZE)
#define PCI_CONFIG_ADDRESS 0xcf8U
#define PCI_CONFIG_DATA 0xcfcU
#define PCI_MAX_BUSES 256U

static struct acpi_mcfg_region mcfg_regions[ACPI_MCFG_MAX_REGIONS];
static size_t mcfg_region_count;
static uint16_t ecam_segment = UINT16_MAX;
static uint8_t ecam_bus = UINT8_MAX;

static inline void
outl(uint16_t port, uint32_t value)
{
	__asm__ volatile ("outl %0, %1" : : "a" (value), "Nd" (port));
}

static inline uint32_t
inl(uint16_t port)
{
	uint32_t value;
	__asm__ volatile ("inl %1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

static bool
config_valid(uint8_t device, uint8_t function, uint16_t offset,
	unsigned int width)
{
	return device < 32 && function < 8 && width >= 8 && width <= 32 &&
	    (width == 8 || width == 16 || width == 32) &&
	    (offset & (width / 8 - 1)) == 0 && offset <= 4096 - width / 8;
}

static const struct acpi_mcfg_region *
find_region(uint16_t segment, uint8_t bus)
{
	for (size_t i = 0; i < mcfg_region_count; i++) {
		const struct acpi_mcfg_region *region = &mcfg_regions[i];

		if (region->segment == segment && bus >= region->start_bus &&
		    bus <= region->end_bus)
			return region;
	}
	return NULL;
}

static bool
map_ecam_bus(const struct acpi_mcfg_region *region, uint8_t bus)
{
	uint64_t virtual_base = vmm_vmalloc_start() + ECAM_VIRTUAL_OFFSET;
	uint64_t physical_base = region->base +
	    ((uint64_t)(bus - region->start_bus) << 20);

	if (ecam_segment == region->segment && ecam_bus == bus)
		return true;
	if (ecam_bus != UINT8_MAX) {
		for (size_t i = 0; i < ECAM_WINDOW_PAGES; i++) {
			uint64_t ignored;
			if (!vmm_unmap_page(vmm_kernel_address_space(),
			    virtual_base + i * PAGE_SIZE, &ignored))
				return false;
		}
		ecam_segment = UINT16_MAX;
		ecam_bus = UINT8_MAX;
	}
	for (size_t i = 0; i < ECAM_WINDOW_PAGES; i++) {
		if (!vmm_map_page(vmm_kernel_address_space(),
		    virtual_base + i * PAGE_SIZE, physical_base + i * PAGE_SIZE,
		    VMM_WRITE | VMM_UNCACHED)) {
			while (i != 0) {
				uint64_t ignored;
				i--;
				(void)vmm_unmap_page(vmm_kernel_address_space(),
				    virtual_base + i * PAGE_SIZE, &ignored);
			}
			return false;
		}
	}
	ecam_segment = region->segment;
	ecam_bus = bus;
	return true;
}

static uint32_t
port_config_read(uint8_t bus, uint8_t device, uint8_t function,
	uint16_t offset)
{
	uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
	    ((uint32_t)device << 11) | ((uint32_t)function << 8) |
	    (offset & 0xfcU);

	outl(PCI_CONFIG_ADDRESS, address);
	return inl(PCI_CONFIG_DATA);
}

static bool
reader_for_device(void *context, uint16_t segment, uint8_t bus,
	uint8_t device, uint8_t function, uint16_t offset, uint32_t *value)
{
	const struct acpi_mcfg_region *region = find_region(segment, bus);

	(void)context;
	if (region != NULL) {
		uint64_t address;
		volatile uint32_t *register_pointer;

		if (!map_ecam_bus(region, bus))
			return false;
		address = vmm_vmalloc_start() + ECAM_VIRTUAL_OFFSET +
		    ((uint64_t)device << 15) + ((uint64_t)function << 12) +
		    (offset & 0xffcU);
		register_pointer = (volatile uint32_t *)(uintptr_t)address;
		*value = *register_pointer;
		return true;
	}
	if (segment != 0 || offset >= 256)
		return false;
	*value = port_config_read(bus, device, function, offset);
	return true;
}

bool
pci_config_read(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, unsigned int width, uint32_t *value)
{
	const struct acpi_mcfg_region *region = find_region(segment, bus);
	uint32_t dword;
	uint16_t aligned = offset & 0xfffcU;

	if (value == NULL || !config_valid(device, function, offset, width))
		return false;
	if (region != NULL && reader_for_device(NULL, segment, bus, device,
	    function, aligned, &dword)) {
		/* ECAM is preferred when a validated MCFG region is available. */
	} else {
		/* Segment zero may also be addressed through PCI Configuration
		 * Mechanism #1. Keep it as a fallback if an ECAM remap/read fails. */
		if (segment != 0 || offset >= 256)
			return false;
		dword = port_config_read(bus, device, function, aligned);
	}
	*value = width == 32 ? dword :
	    (dword >> ((offset & 3U) * 8)) & ((1U << width) - 1);
	return true;
}

bool
pci_config_write(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, uint16_t offset, unsigned int width, uint32_t value)
{
	const struct acpi_mcfg_region *region = find_region(segment, bus);
	uint16_t aligned = offset & 0xfffcU;
	uint32_t dword;
	uint32_t mask;

	/* This write path is called only for AHCI controller setup. */
	if (offset != 4 || width != 16 ||
	    !config_valid(device, function, offset, width))
		return false;
	if (region != NULL && segment != 0) {
		uint64_t address;
		volatile uint32_t *register_pointer;

		if (!map_ecam_bus(region, bus))
			return false;
		address = vmm_vmalloc_start() + ECAM_VIRTUAL_OFFSET +
		    ((uint64_t)device << 15) + ((uint64_t)function << 12) + aligned;
		register_pointer = (volatile uint32_t *)(uintptr_t)address;
		dword = *register_pointer;
	} else {
		if (segment != 0 || offset >= 256)
			return false;
		dword = port_config_read(bus, device, function, aligned);
	}
	mask = width == 32 ? UINT32_MAX :
	    ((1U << width) - 1) << ((offset & 3U) * 8);
	dword = (dword & ~mask) | ((value << ((offset & 3U) * 8)) & mask);
	if (region != NULL && segment != 0) {
		uint64_t address = vmm_vmalloc_start() + ECAM_VIRTUAL_OFFSET +
		    ((uint64_t)device << 15) + ((uint64_t)function << 12) + aligned;
		*(volatile uint32_t *)(uintptr_t)address = dword;
	} else {
		uint32_t address = 0x80000000U | ((uint32_t)bus << 16) |
		    ((uint32_t)device << 11) | ((uint32_t)function << 8) | aligned;
		outl(PCI_CONFIG_ADDRESS, address);
		outl(PCI_CONFIG_DATA, dword);
	}
	return true;
}

bool
pci_bar_decode(const struct pci_device_info *device, unsigned int index,
	struct pci_bar_resource *resource)
{
	unsigned int bar_count;
	uint32_t low;
	uint32_t attributes;

	if (device == NULL || resource == NULL)
		return false;
	bar_count = (device->header_type & 0x7fU) == 0 ? 6 :
	    (device->header_type & 0x7fU) == 1 ? 2 : 0;
	if (bar_count == 0 || index >= bar_count)
		return false;
	for (unsigned int previous = 0; previous < index; previous++) {
		uint32_t previous_low = device->bars[previous];

		if (previous_low != 0 && previous_low != UINT32_MAX &&
		    (previous_low & 1U) == 0 &&
		    ((previous_low >> 1) & 3U) == 2U) {
			if (previous + 1 == index)
				return false;
			previous++;
		}
	}
	low = device->bars[index];
	if (low == UINT32_MAX)
		return false;
	if (low == 0) {
		*resource = (struct pci_bar_resource){
			.type = PCI_BAR_UNUSED,
			.index = (uint8_t)index,
			.slots = 1
		};
		return true;
	}
	if ((low & 1U) != 0) {
		*resource = (struct pci_bar_resource){
			.type = PCI_BAR_IO,
			.base = low & ~0x3U,
			.index = (uint8_t)index,
			.slots = 1
		};
		return true;
	}
	attributes = low & 0xfU;
	switch ((low >> 1) & 3U) {
	case 0:
		*resource = (struct pci_bar_resource){
			.type = PCI_BAR_MEMORY32,
			.base = low & ~0xfU,
			.index = (uint8_t)index,
			.slots = 1,
			.prefetchable = (attributes & 8U) != 0
		};
		return true;
	case 2:
		if (index + 1 >= bar_count || index == 5)
			return false;
		*resource = (struct pci_bar_resource){
			.type = PCI_BAR_MEMORY64,
			.base = ((uint64_t)device->bars[index + 1] << 32) |
			    (low & ~0xfU),
			.index = (uint8_t)index,
			.slots = 2,
			.prefetchable = (attributes & 8U) != 0
		};
		return true;
	case 1:
	case 3:
	default:
		return false;
	}
}

static bool
scan_function(uint16_t segment, uint8_t bus, uint8_t device,
	uint8_t function, pci_config_reader reader, void *reader_context,
	pci_device_callback callback, void *callback_context,
	uint8_t *secondary_bus)
{
	uint32_t id;
	uint32_t class_revision;
	uint32_t header;
	uint32_t buses;
	uint32_t command;
	struct pci_device_info info;
	unsigned int bar_count;

	*secondary_bus = UINT8_MAX;
	if (!reader(reader_context, segment, bus, device, function, 0, &id))
		return false;
	if ((id & 0xffffU) == 0xffffU)
		return true;
	if (!reader(reader_context, segment, bus, device, function, 8,
	    &class_revision) || !reader(reader_context, segment, bus, device,
	    function, 0x0c, &header) || !reader(reader_context, segment, bus,
	    device, function, 4, &command))
		return false;
	info = (struct pci_device_info){
		.segment = segment,
		.bus = bus,
		.device = device,
		.function = function,
		.class_code = (uint8_t)(class_revision >> 24),
		.subclass = (uint8_t)(class_revision >> 16),
		.programming_interface = (uint8_t)(class_revision >> 8),
		.header_type = (uint8_t)(header >> 16),
		.vendor_id = (uint16_t)id,
		.device_id = (uint16_t)(id >> 16),
		.command = (uint16_t)command
	};
	bar_count = (info.header_type & 0x7fU) == 0 ? 6 :
	    (info.header_type & 0x7fU) == 1 ? 2 : 0;
	for (unsigned int i = 0; i < bar_count; i++) {
		if (!reader(reader_context, segment, bus, device, function,
		    (uint16_t)(0x10 + i * 4), &info.bars[i]))
			return false;
	}
	if (info.class_code == 0x06 && info.subclass == 0x04 &&
	    (info.header_type & 0x7fU) == 1) {
		if (!reader(reader_context, segment, bus, device, function, 0x18,
		    &buses))
			return false;
		*secondary_bus = (uint8_t)(buses >> 8);
	}
	return callback(&info, callback_context);
}

static bool
scan_bus(uint16_t segment, uint8_t bus, pci_config_reader reader,
	void *reader_context, pci_device_callback callback, void *callback_context,
	uint8_t *secondary_buses, size_t *secondary_count)
{
	for (uint8_t device = 0; device < 32; device++) {
		uint32_t id;
		uint32_t header;
		uint8_t functions;

		if (!reader(reader_context, segment, bus, device, 0, 0, &id))
			return false;
		if ((id & 0xffffU) == 0xffffU)
			continue;
		if (!reader(reader_context, segment, bus, device, 0, 0x0c, &header))
			return false;
		functions = ((header >> 16) & 0x80U) != 0 ? 8 : 1;
		for (uint8_t function = 0; function < functions; function++) {
			uint8_t secondary;
			if (!scan_function(segment, bus, device, function, reader,
			    reader_context, callback, callback_context, &secondary))
				return false;
			if (secondary != UINT8_MAX && secondary != bus &&
			    *secondary_count < PCI_MAX_BUSES)
			secondary_buses[(*secondary_count)++] = secondary;
		}
	}
	return true;
}

bool
pci_enumerate_with_reader(pci_config_reader reader, void *reader_context,
	const struct acpi_mcfg_region *regions, size_t region_count,
	pci_device_callback callback, void *callback_context)
{
	bool visited[PCI_MAX_BUSES] = {0};
	uint8_t queue[PCI_MAX_BUSES];
	size_t head = 0;
	size_t tail = 0;

	if (reader == NULL || callback == NULL ||
	    (region_count != 0 && regions == NULL) ||
	    region_count > ACPI_MCFG_MAX_REGIONS)
		return false;
	if (region_count != 0) {
		for (size_t r = 0; r < region_count; r++) {
			for (unsigned int bus = regions[r].start_bus;
			    bus <= regions[r].end_bus; bus++) {
				uint8_t secondary[PCI_MAX_BUSES];
				size_t secondary_count = 0;
				if (!scan_bus(regions[r].segment, (uint8_t)bus, reader,
				    reader_context, callback, callback_context, secondary,
				    &secondary_count))
					return false;
			}
		}
		return true;
	}
	queue[tail++] = 0;
	visited[0] = true;
	while (head < tail) {
		uint8_t bus = queue[head++];
		uint8_t secondary[PCI_MAX_BUSES];
		size_t secondary_count = 0;

		if (!scan_bus(0, bus, reader, reader_context, callback,
		    callback_context, secondary, &secondary_count))
			return false;
		for (size_t i = 0; i < secondary_count; i++) {
			uint8_t child = secondary[i];
			if (!visited[child]) {
				if (tail == PCI_MAX_BUSES)
					return false;
				visited[child] = true;
				queue[tail++] = child;
			}
		}
	}
	return true;
}

bool
pci_enumerate(pci_device_callback callback, void *context)
{
	if (callback == NULL)
		return false;
	if (ecam_segment != UINT16_MAX) {
		uint64_t virtual_base = vmm_vmalloc_start() + ECAM_VIRTUAL_OFFSET;
		for (size_t i = 0; i < ECAM_WINDOW_PAGES; i++) {
			uint64_t ignored;
			if (!vmm_unmap_page(vmm_kernel_address_space(),
			    virtual_base + i * PAGE_SIZE, &ignored))
				return false;
		}
		ecam_segment = UINT16_MAX;
		ecam_bus = UINT8_MAX;
	}
	memset(mcfg_regions, 0, sizeof(mcfg_regions));
	mcfg_region_count = acpi_mcfg_regions(mcfg_regions,
	    ACPI_MCFG_MAX_REGIONS);
	return pci_enumerate_with_reader(reader_for_device, NULL, mcfg_regions,
	    mcfg_region_count, callback, context);
}
