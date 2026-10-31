#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/pci_resource.h"
#include "kernel/dma.h"
#include "kernel/e1000.h"
#include "kernel/e1000_profile.h"
#include "kernel/log.h"
#include "kernel/vmm.h"

#define PCI_COMMAND_OFFSET 0x04U
#define PCI_COMMAND_MEMORY 0x0002U
#define PCI_COMMAND_BUS_MASTER 0x0004U

struct e1000_instance {
	enum e1000_lifecycle_state state;
	enum e1000_failure failure;
	struct pci_device_info pci;
	struct e1000_qemu_resources resources;
	struct pci_mmio_lease mmio;
	volatile uint8_t *registers;
	uint16_t pci_command_after;
	bool discovered;
};

static struct e1000_instance device;

static bool
register_range_valid(uint32_t offset)
{
	return e1000_register_access_valid(offset, sizeof(uint32_t),
	    E1000_QEMU_BAR0_APERTURE);
}

static bool
mmio_read32(void *context, uint32_t offset, uint32_t *value)
{
	volatile uint32_t *register_address;
	volatile uint8_t *base = context;

	if (base == NULL || value == NULL || !register_range_valid(offset))
		return false;
	register_address = (volatile uint32_t *)(base + offset);
	__asm__ volatile ("mfence" : : : "memory");
	*value = *register_address;
	__asm__ volatile ("mfence" : : : "memory");
	return true;
}

static bool
mmio_write32(void *context, uint32_t offset, uint32_t value)
{
	volatile uint32_t *register_address;
	volatile uint8_t *base = context;

	if (base == NULL || !register_range_valid(offset))
		return false;
	register_address = (volatile uint32_t *)(base + offset);
	*register_address = value;
	/* Order the UC register write and make it visible before polling/readback. */
	__asm__ volatile ("mfence" : : : "memory");
	return true;
}

bool
e1000_discover(const struct pci_device_info *pci)
{
	if (pci == NULL || pci->vendor_id != E1000_QEMU_VENDOR_ID ||
	    pci->device_id != E1000_QEMU_DEVICE_ID)
		return false;
	if (device.discovered) {
		device.failure = E1000_FAILURE_DUPLICATE;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	device.pci = *pci;
	device.discovered = true;
	device.state = E1000_STATE_DISCOVERED;
	device.failure = E1000_FAILURE_NONE;
	return true;
}

static bool
set_pci_decode_without_bus_master(void)
{
	uint32_t command;
	uint32_t verified;
	uint16_t updated;

	if (!pci_config_read(device.pci.segment, device.pci.bus, device.pci.device,
	    device.pci.function, PCI_COMMAND_OFFSET, 16, &command)) {
		klog("OpenSillicon: e1000 PCI command read failed at ");
		klog_u64(device.pci.segment);
		klog(":");
		klog_u64(device.pci.bus);
		klog(":");
		klog_u64(device.pci.device);
		klog(":");
		klog_u64(device.pci.function);
		klog("\n");
		return false;
	}
	updated = (uint16_t)((command | PCI_COMMAND_MEMORY) &
	    ~PCI_COMMAND_BUS_MASTER);
	if (!pci_config_write(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    updated)) {
		klog("OpenSillicon: e1000 PCI command write failed\n");
		return false;
	}
	if (!pci_config_read(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    &verified)) {
		klog("OpenSillicon: e1000 PCI command readback failed\n");
		return false;
	}
	if ((verified & PCI_COMMAND_MEMORY) == 0 ||
	    (verified & PCI_COMMAND_BUS_MASTER) != 0) {
		klog("OpenSillicon: e1000 PCI command value mismatch ");
		klog_u64(verified);
		klog(" expected memory decode set and bus master clear\n");
	}
	device.pci_command_after = (uint16_t)verified;
	return (verified & PCI_COMMAND_MEMORY) != 0 &&
	    (verified & PCI_COMMAND_BUS_MASTER) == 0;
}

static bool
disable_pci_decode(void)
{
	uint32_t command;
	uint32_t verified;
	uint16_t updated;

	if (!pci_config_read(device.pci.segment, device.pci.bus, device.pci.device,
	    device.pci.function, PCI_COMMAND_OFFSET, 16, &command))
		return false;
	updated = (uint16_t)(command &
	    ~(PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER));
	return pci_config_write(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    updated) && pci_config_read(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    &verified) && (verified &
	    (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) == 0;
}

bool
e1000_initialize(void)
{
	struct e1000_register_ops operations;
	volatile void *mapped;
	uint32_t control;
	uint32_t status;
	bool reset_timed_out = false;

	if (device.state == E1000_STATE_INITIALIZED)
		return true;
	if (!device.discovered || device.state == E1000_STATE_FAILED ||
	    device.state == E1000_STATE_STOPPED)
		return false;
	if (!dma_identity_addressing_available()) {
		device.failure = E1000_FAILURE_DMA_CONTRACT;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	if (!e1000_qemu_profile_validate(&device.pci, &device.resources)) {
		device.failure = E1000_FAILURE_PROFILE;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	device.state = E1000_STATE_RESOURCES_VALIDATED;
	if (!set_pci_decode_without_bus_master()) {
		device.failure = E1000_FAILURE_PCI_CONFIG;
		device.state = E1000_STATE_FAILED;
		(void)disable_pci_decode();
		return false;
	}
	if (!pci_mmio_map(&device.resources.register_mmio, 0,
	    E1000_QEMU_BAR0_APERTURE, &device.mmio)) {
		klog("OpenSillicon: e1000 BAR0 mapping rejected, physical base ");
		klog_u64(device.resources.register_mmio.base);
		klog(" aperture ");
		klog_u64(device.resources.register_mmio.aperture_size);
		klog(" reserved-memory-safe ");
		klog(vmm_physical_range_mmio_safe(
		    device.resources.register_mmio.base,
		    E1000_QEMU_BAR0_APERTURE) ? "yes\n" : "no\n");
		device.failure = E1000_FAILURE_MAP;
		device.state = E1000_STATE_FAILED;
		(void)disable_pci_decode();
		return false;
	}
	if (!pci_mmio_lease_address(&device.mmio, &mapped)) {
		device.failure = E1000_FAILURE_MAP;
		device.state = E1000_STATE_FAILED;
		(void)disable_pci_decode();
		(void)pci_mmio_release(&device.mmio);
		return false;
	}
	device.registers = mapped;
	device.state = E1000_STATE_MAPPED;
	operations = (struct e1000_register_ops){
		.read32 = mmio_read32,
		.write32 = mmio_write32,
		.context = (void *)device.registers
	};
	device.state = E1000_STATE_RESET_IN_PROGRESS;
	if (!e1000_reset_and_validate(&operations, E1000_RESET_POLL_LIMIT,
	    &reset_timed_out)) {
		device.failure = reset_timed_out ? E1000_FAILURE_RESET_TIMEOUT :
		    E1000_FAILURE_POST_RESET;
		device.state = E1000_STATE_FAILED;
		/* Retain the mapping unless PCI decode is verified disabled. Bus
		 * mastering was cleared before mapping, so no DMA was started. */
		if (disable_pci_decode() && pci_mmio_release(&device.mmio))
			device.registers = NULL;
		return false;
	}
	device.state = E1000_STATE_INITIALIZED;
	if (!mmio_read32((void *)device.registers, E1000_REG_CTRL, &control) ||
	    !mmio_read32((void *)device.registers, E1000_REG_STATUS, &status)) {
		device.failure = E1000_FAILURE_POST_RESET;
		device.state = E1000_STATE_FAILED;
		if (disable_pci_decode() && pci_mmio_release(&device.mmio))
			device.registers = NULL;
		return false;
	}
	klog("OpenSillicon: e1000 profile 8086:100e BAR0 ");
	klog_u64(device.resources.register_mmio.base);
	klog(" size ");
	klog_u64(device.resources.register_mmio.aperture_size);
	klog(" PCI command ");
	klog_u64(device.pci_command_after);
	klog(" CTRL ");
	klog_u64(control);
	klog(" STATUS ");
	klog_u64(status);
	klog(" reset complete; bus mastering remains disabled\n");
	return true;
}

enum e1000_lifecycle_state
e1000_state(void)
{
	return device.state;
}

enum e1000_failure
e1000_last_failure(void)
{
	return device.failure;
}
