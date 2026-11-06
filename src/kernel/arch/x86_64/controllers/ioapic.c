#include <stdint.h>

#include "arch/x86_64/ioapic.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096ULL
#define IOAPIC_MAX_REDIR 120U
#define IOAPIC_REG_SELECT 0x00U
#define IOAPIC_REG_WINDOW 0x10U
#define IOAPIC_REG_VERSION 0x01U
#define IOAPIC_REG_REDIR_BASE 0x10U
#define IOAPIC_REDIR_MASK (1U << 16)

struct ioapic_state {
	volatile uint32_t *registers;
	uint32_t gsi_base;
	uint32_t gsi_end;
};

static struct ioapic_state controllers[ACPI_MADT_MAX_IOAPICS];
static size_t controller_count;

static uint32_t
ioapic_read(struct ioapic_state *controller, uint8_t index)
{
	controller->registers[IOAPIC_REG_SELECT / 4] = index;
	return controller->registers[IOAPIC_REG_WINDOW / 4];
}

static void
ioapic_write(struct ioapic_state *controller, uint8_t index, uint32_t value)
{
	controller->registers[IOAPIC_REG_SELECT / 4] = index;
	controller->registers[IOAPIC_REG_WINDOW / 4] = value;
}

bool
ioapic_init(const struct acpi_madt_info *madt)
{
	uint64_t virtual_base;
	size_t i;

	if (madt == NULL || madt->ioapic_count == 0 ||
	    madt->ioapic_count > ACPI_MADT_MAX_IOAPICS || controller_count != 0)
		return false;
	virtual_base = vmm_vmalloc_start() + PAGE_SIZE;
	for (i = 0; i < madt->ioapic_count; i++) {
		const struct acpi_madt_ioapic *entry = &madt->ioapics[i];
		struct ioapic_state *controller = &controllers[i];
		uint64_t physical = entry->address & ~(PAGE_SIZE - 1);
		uint64_t virtual_address = virtual_base + i * PAGE_SIZE;
		uint32_t version;
		uint32_t redir_count;
		uint32_t redir;
		size_t previous;

		if (entry->address == 0 || (entry->address & 0xfffU) != 0 ||
		    virtual_address >= vmm_vmalloc_end())
			return false;
		for (previous = 0; previous < i; previous++) {
			if (controllers[previous].gsi_base == entry->gsi_base)
				return false;
		}
		if (!vmm_map_page(vmm_kernel_address_space(), virtual_address,
		    physical, VMM_WRITE | VMM_UNCACHED))
			return false;
		controller->registers = (volatile uint32_t *)(uintptr_t)(
		    virtual_address + (entry->address & (PAGE_SIZE - 1)));
		version = ioapic_read(controller, IOAPIC_REG_VERSION);
		redir_count = ((version >> 16) & 0xffU) + 1;
		if (redir_count == 0 || redir_count > IOAPIC_MAX_REDIR ||
		    entry->gsi_base > UINT32_MAX - redir_count)
			return false;
		controller->gsi_base = entry->gsi_base;
		controller->gsi_end = entry->gsi_base + redir_count;
		for (previous = 0; previous < i; previous++) {
			if (controller->gsi_base < controllers[previous].gsi_end &&
			    controllers[previous].gsi_base < controller->gsi_end)
				return false;
		}
		for (redir = 0; redir < redir_count; redir++) {
			uint8_t low = (uint8_t)(IOAPIC_REG_REDIR_BASE + 2 * redir);

			ioapic_write(controller, low, IOAPIC_REDIR_MASK);
			ioapic_write(controller, (uint8_t)(low + 1), 0);
		}
		controller_count++;
	}
	return true;
}
