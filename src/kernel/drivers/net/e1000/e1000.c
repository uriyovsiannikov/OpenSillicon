#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/pci_resource.h"
#include "kernel/dma.h"
#include "kernel/dma_ring.h"
#include "kernel/e1000.h"
#include "kernel/e1000_profile.h"
#include "kernel/e1000_ring.h"
#include "kernel/log.h"
#include "kernel/netbuf.h"
#include "kernel/netdev.h"
#include "kernel/vmm.h"

#define PCI_COMMAND_OFFSET 0x04U
#define PCI_COMMAND_MEMORY 0x0002U
#define PCI_COMMAND_BUS_MASTER 0x0004U

#define E1000_REG_CTRL 0x0000U
#define E1000_REG_STATUS 0x0008U
#define E1000_REG_ICR 0x00c0U
#define E1000_REG_IMC 0x00d8U
#define E1000_REG_RCTL 0x0100U
#define E1000_REG_RAL 0x5400U
#define E1000_REG_RAH 0x5404U
#define E1000_REG_TCTL 0x0400U
#define E1000_REG_TIPG 0x0410U
#define E1000_REG_TDBAL 0x3800U
#define E1000_REG_TDBAH 0x3804U
#define E1000_REG_TDLEN 0x3808U
#define E1000_REG_TDH 0x3810U
#define E1000_REG_TDT 0x3818U
#define E1000_REG_RDBAL 0x2800U
#define E1000_REG_RDBAH 0x2804U
#define E1000_REG_RDLEN 0x2808U
#define E1000_REG_RDH 0x2810U
#define E1000_REG_RDT 0x2818U

#define E1000_STATUS_LINK_UP (1U << 1)
#define E1000_RAH_ADDRESS_VALID (1U << 31)
#define E1000_CTRL_RST (1U << 26)
#define E1000_RCTL_ENABLE (1U << 1)
#define E1000_RCTL_BROADCAST_ACCEPT (1U << 15)
#define E1000_RCTL_STRIP_CRC (1U << 26)
#define E1000_TCTL_ENABLE (1U << 1)
#define E1000_TCTL_PAD_SHORT_PACKETS (1U << 3)
#define E1000_TCTL_COLLISION_THRESHOLD (0x10U << 4)
#define E1000_TCTL_COLLISION_DISTANCE (0x40U << 12)
#define E1000_TIPG_82540EM 0x0060200aU

#define E1000_TX_RING_BYTES \
	(E1000_TX_RING_COUNT * sizeof(struct e1000_tx_descriptor))
#define E1000_RX_RING_BYTES \
	(E1000_RX_RING_COUNT * sizeof(struct e1000_rx_descriptor))

struct e1000_tx_slot {
	struct dma_buffer buffer;
	struct dma_buffer_info memory;
	struct netbuf_ref packet;
	bool allocated;
	bool dma_owned;
	bool in_flight;
};

struct e1000_rx_slot {
	struct dma_buffer buffer;
	struct dma_buffer_info memory;
	bool allocated;
	bool dma_owned;
};

struct e1000_instance {
	enum e1000_lifecycle_state state;
	enum e1000_failure failure;
	struct pci_device_info pci;
	struct pci_device_lease pci_lease;
	struct e1000_qemu_resources resources;
	struct pci_mmio_lease mmio;
	volatile uint8_t *registers;
	uint16_t pci_command_after;
	bool discovered;
	bool bus_master_enabled;
	bool engines_enabled;
	bool tx_ring_started;
	bool rx_ring_started;
	struct dma_buffer tx_ring_buffer;
	struct dma_buffer rx_ring_buffer;
	struct dma_buffer_info tx_ring_memory;
	struct dma_buffer_info rx_ring_memory;
	bool tx_ring_allocated;
	bool rx_ring_allocated;
	struct e1000_tx_descriptor *tx_descriptors;
	struct e1000_rx_descriptor *rx_descriptors;
	uint8_t tx_owners[E1000_TX_RING_COUNT];
	uint8_t rx_owners[E1000_RX_RING_COUNT];
	struct dma_descriptor_ring tx_ring;
	struct dma_descriptor_ring rx_ring;
	struct e1000_tx_slot tx[E1000_TX_RING_COUNT];
	struct e1000_rx_slot rx[E1000_RX_RING_COUNT];
	uint32_t tx_tail;
	uint32_t tx_reclaim;
	uint32_t rx_head;
	struct net_device network_device;
};

static struct e1000_instance device;

static bool e1000_register_range_valid(uint32_t offset);
static bool e1000_mmio_read32(void *context, uint32_t offset,
	uint32_t *value);
static bool e1000_mmio_write32(void *context, uint32_t offset,
	uint32_t value);
static int e1000_initialize_callback(struct net_device *network_device);
static void e1000_shutdown_callback(struct net_device *network_device);
static bool e1000_link_up_callback(const struct net_device *network_device);
static int e1000_transmit_callback(struct net_device *network_device,
	struct netbuf_ref packet);
static int e1000_receive_callback(struct net_device *network_device,
	size_t budget, struct netbuf_ref *packets, size_t capacity,
	size_t *count);
static int e1000_wait_transmit_callback(struct net_device *network_device,
	struct netbuf_ref packet, size_t poll_limit);

static const struct net_device_ops network_operations = {
	.initialize = e1000_initialize_callback,
	.shutdown = e1000_shutdown_callback,
	.link_up = e1000_link_up_callback,
	.transmit = e1000_transmit_callback,
	.wait_transmit = e1000_wait_transmit_callback,
	.receive = e1000_receive_callback
};

static bool
e1000_register_range_valid(uint32_t offset)
{
	return e1000_register_access_valid(offset, sizeof(uint32_t),
	    E1000_QEMU_BAR0_APERTURE);
}

static bool
e1000_mmio_read32(void *context, uint32_t offset, uint32_t *value)
{
	volatile uint32_t *register_address;
	volatile uint8_t *base = context;

	if (base == NULL || value == NULL || !e1000_register_range_valid(offset))
		return false;
	register_address = (volatile uint32_t *)(base + offset);
	__asm__ volatile ("mfence" : : : "memory");
	*value = *register_address;
	__asm__ volatile ("mfence" : : : "memory");
	return true;
}

static bool
e1000_mmio_write32(void *context, uint32_t offset, uint32_t value)
{
	volatile uint32_t *register_address;
	volatile uint8_t *base = context;

	if (base == NULL || !e1000_register_range_valid(offset))
		return false;
	register_address = (volatile uint32_t *)(base + offset);
	*register_address = value;
	__asm__ volatile ("mfence" : : : "memory");
	return true;
}

static bool
e1000_read(uint32_t offset, uint32_t *value)
{
	return e1000_mmio_read32((void *)device.registers, offset, value);
}

static bool
e1000_write(uint32_t offset, uint32_t value)
{
	return e1000_mmio_write32((void *)device.registers, offset, value);
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
	if (!pci_device_claim(pci, &device.pci_lease))
		return false;
	device.pci = *pci;
	device.discovered = true;
	device.state = E1000_STATE_DISCOVERED;
	device.failure = E1000_FAILURE_NONE;
	device.network_device = (struct net_device){
		.name = "e1000",
		.operations = &network_operations,
		.driver_context = &device,
		.mtu = 1500,
		.state = NET_DEVICE_DOWN
	};
	return true;
}

static bool
set_pci_decode_without_bus_master(void)
{
	uint32_t command;
	uint32_t verified;
	uint16_t updated;

	if (!pci_config_read(device.pci.segment, device.pci.bus, device.pci.device,
	    device.pci.function, PCI_COMMAND_OFFSET, 16, &command))
		return false;
	updated = (uint16_t)((command | PCI_COMMAND_MEMORY) &
	    ~PCI_COMMAND_BUS_MASTER);
	if (!pci_config_write(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    updated) || !pci_config_read(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    &verified))
		return false;
	device.pci_command_after = (uint16_t)verified;
	return (verified & PCI_COMMAND_MEMORY) != 0 &&
	    (verified & PCI_COMMAND_BUS_MASTER) == 0;
}

static bool
enable_pci_bus_master(void)
{
	uint32_t command;
	uint32_t verified;

	if (!pci_config_read(device.pci.segment, device.pci.bus, device.pci.device,
	    device.pci.function, PCI_COMMAND_OFFSET, 16, &command) ||
	    (command & PCI_COMMAND_MEMORY) == 0 ||
	    !pci_config_write(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    (uint16_t)(command | PCI_COMMAND_BUS_MASTER)))
		return false;
	/* Once written, regard Bus Mastering as possibly enabled until readback
	 * proves the transition succeeded. */
	device.bus_master_enabled = true;
	if (!pci_config_read(device.pci.segment, device.pci.bus, device.pci.device,
	    device.pci.function, PCI_COMMAND_OFFSET, 16, &verified) ||
	    (verified & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) !=
	    (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER))
		return false;
	device.pci_command_after = (uint16_t)verified;
	return true;
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
	if (!pci_config_write(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    updated) || !pci_config_read(device.pci.segment, device.pci.bus,
	    device.pci.device, device.pci.function, PCI_COMMAND_OFFSET, 16,
	    &verified) || (verified & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)))
		return false;
	device.pci_command_after = (uint16_t)verified;
	device.bus_master_enabled = false;
	return true;
}

static bool
alloc_dma_buffer(size_t size, size_t alignment, struct dma_buffer *buffer,
	struct dma_buffer_info *info)
{
	return dma_alloc(size, alignment, 64, buffer) &&
	    dma_buffer_query(*buffer, info) && info->device_address ==
	    info->physical_address && info->requested_size >= size;
}

static bool
allocate_rings(void)
{
	if (!alloc_dma_buffer(E1000_TX_RING_BYTES, 128, &device.tx_ring_buffer,
	    &device.tx_ring_memory))
		return false;
	device.tx_ring_allocated = true;
	if (!alloc_dma_buffer(E1000_RX_RING_BYTES, 128, &device.rx_ring_buffer,
	    &device.rx_ring_memory))
		return false;
	device.rx_ring_allocated = true;
	if (!e1000_ring_geometry_valid(device.tx_ring_memory.device_address,
	    E1000_TX_RING_BYTES, sizeof(*device.tx_descriptors),
	    E1000_TX_RING_COUNT) ||
	    !e1000_ring_geometry_valid(device.rx_ring_memory.device_address,
	    E1000_RX_RING_BYTES, sizeof(*device.rx_descriptors),
	    E1000_RX_RING_COUNT))
		return false;
	device.tx_descriptors = device.tx_ring_memory.cpu_virtual_address;
	device.rx_descriptors = device.rx_ring_memory.cpu_virtual_address;
	if (!dma_ring_init(&device.tx_ring, device.tx_owners,
	    sizeof(device.tx_owners), E1000_TX_RING_COUNT, device.tx_ring_buffer,
	    E1000_TX_RING_BYTES) ||
	    !dma_ring_init(&device.rx_ring, device.rx_owners,
	    sizeof(device.rx_owners), E1000_RX_RING_COUNT, device.rx_ring_buffer,
	    E1000_RX_RING_BYTES) ||
	    !dma_ring_start(&device.tx_ring))
		return false;
	device.tx_ring_started = true;
	if (!dma_ring_start(&device.rx_ring))
		return false;
	device.rx_ring_started = true;
	for (uint32_t i = 0; i < E1000_TX_RING_COUNT; i++) {
		struct e1000_tx_descriptor *descriptor = &device.tx_descriptors[i];

		if (!alloc_dma_buffer(E1000_DMA_BUFFER_SIZE, 16,
		    &device.tx[i].buffer, &device.tx[i].memory))
			return false;
		device.tx[i].allocated = true;
		descriptor->status = E1000_TXD_STAT_DD;
	}
	for (uint32_t i = 0; i < E1000_RX_RING_COUNT; i++) {
		struct e1000_rx_descriptor *descriptor = &device.rx_descriptors[i];
		uint64_t address;

		if (!alloc_dma_buffer(E1000_DMA_BUFFER_SIZE, 16,
		    &device.rx[i].buffer, &device.rx[i].memory))
			return false;
		device.rx[i].allocated = true;
		descriptor->buffer_address = device.rx[i].memory.device_address;
		if (!dma_device_acquire(device.rx[i].buffer, &address) ||
		    address != descriptor->buffer_address)
			return false;
		device.rx[i].dma_owned = true;
		if (!dma_ring_publish(&device.rx_ring, i))
			return false;
	}
	return true;
}

static bool
program_rings(void)
{
	uint64_t tx = device.tx_ring_memory.device_address;
	uint64_t rx = device.rx_ring_memory.device_address;

	/* Engines are reset/disabled and PCI Bus Mastering is still clear here. */
	return e1000_write(E1000_REG_IMC, UINT32_MAX) &&
	    e1000_read(E1000_REG_ICR, &(uint32_t){0}) &&
	    e1000_write(E1000_REG_RCTL, 0) &&
	    e1000_write(E1000_REG_TCTL, 0) &&
	    e1000_write(E1000_REG_TIPG, E1000_TIPG_82540EM) &&
	    e1000_write(E1000_REG_TDBAL, (uint32_t)tx) &&
	    e1000_write(E1000_REG_TDBAH, (uint32_t)(tx >> 32)) &&
	    e1000_write(E1000_REG_TDLEN, E1000_TX_RING_BYTES) &&
	    e1000_write(E1000_REG_TDH, 0) &&
	    e1000_write(E1000_REG_TDT, 0) &&
	    e1000_write(E1000_REG_RDBAL, (uint32_t)rx) &&
	    e1000_write(E1000_REG_RDBAH, (uint32_t)(rx >> 32)) &&
	    e1000_write(E1000_REG_RDLEN, E1000_RX_RING_BYTES) &&
	    e1000_write(E1000_REG_RDH, 0) &&
	    e1000_write(E1000_REG_RDT, E1000_RX_RING_COUNT - 1);
}

static bool
enable_engines(void)
{
	uint32_t rctl = E1000_RCTL_ENABLE | E1000_RCTL_BROADCAST_ACCEPT |
	    E1000_RCTL_STRIP_CRC;
	uint32_t tctl = E1000_TCTL_ENABLE | E1000_TCTL_PAD_SHORT_PACKETS |
	    E1000_TCTL_COLLISION_THRESHOLD | E1000_TCTL_COLLISION_DISTANCE;
	uint32_t readback;

	if (!e1000_write(E1000_REG_TCTL, tctl) ||
	    !e1000_write(E1000_REG_RCTL, rctl) ||
	    !e1000_read(E1000_REG_TCTL, &readback) ||
	    (readback & E1000_TCTL_ENABLE) == 0 ||
	    !e1000_read(E1000_REG_RCTL, &readback) ||
	    (readback & E1000_RCTL_ENABLE) == 0)
		return false;
	device.engines_enabled = true;
	return true;
}

static bool
read_mac_address(uint8_t address[6])
{
	uint32_t low;
	uint32_t high;
	bool nonzero = false;
	bool not_broadcast = false;

	if (address == NULL || !e1000_read(E1000_REG_RAL, &low) ||
	    !e1000_read(E1000_REG_RAH, &high) ||
	    (high & E1000_RAH_ADDRESS_VALID) == 0)
		return false;
	for (size_t i = 0; i < 4; i++)
		address[i] = (uint8_t)(low >> (i * 8));
	address[4] = (uint8_t)high;
	address[5] = (uint8_t)(high >> 8);
	for (size_t i = 0; i < 6; i++) {
		nonzero |= address[i] != 0;
		not_broadcast |= address[i] != 0xff;
	}
	return nonzero && not_broadcast && (address[0] & 1U) == 0;
}

static bool
complete_dma_buffer_if_owned(struct dma_buffer buffer, bool *owned)
{
	if (!*owned)
		return true;
	if (!dma_device_complete(buffer))
		return false;
	*owned = false;
	return true;
}

static bool
release_dma_resources(bool hardware_stopped)
{
	bool success = true;

	if (!hardware_stopped)
		return false;
	for (size_t i = 0; i < E1000_TX_RING_COUNT; i++) {
		if (device.tx[i].dma_owned &&
		    !complete_dma_buffer_if_owned(device.tx[i].buffer,
		    &device.tx[i].dma_owned))
			success = false;
		if (device.tx[i].in_flight) {
			if (!netbuf_transfer(device.tx[i].packet, NETBUF_OWNER_DEVICE,
			    NETBUF_OWNER_CORE) ||
			    !netbuf_release(device.tx[i].packet, NETBUF_OWNER_CORE))
				success = false;
			device.tx[i].in_flight = false;
		}
	}
	for (size_t i = 0; i < E1000_RX_RING_COUNT; i++)
		if (device.rx[i].dma_owned &&
		    !complete_dma_buffer_if_owned(device.rx[i].buffer,
		    &device.rx[i].dma_owned))
			success = false;
	if (device.tx_ring_started) {
		if (!dma_ring_quiesce(&device.tx_ring, true))
			success = false;
		else
			device.tx_ring_started = false;
	}
	if (device.rx_ring_started) {
		if (!dma_ring_quiesce(&device.rx_ring, true))
			success = false;
		else
			device.rx_ring_started = false;
	}
	if (!success)
		return false;
	for (size_t i = 0; i < E1000_TX_RING_COUNT; i++) {
		if (device.tx[i].allocated) {
			if (!dma_free(device.tx[i].buffer))
				success = false;
			else
				device.tx[i].allocated = false;
		}
	}
	for (size_t i = 0; i < E1000_RX_RING_COUNT; i++) {
		if (device.rx[i].allocated) {
			if (!dma_free(device.rx[i].buffer))
				success = false;
			else
				device.rx[i].allocated = false;
		}
	}
	if (device.tx_ring_allocated) {
		if (!dma_free(device.tx_ring_buffer))
			success = false;
		else
			device.tx_ring_allocated = false;
	}
	if (device.rx_ring_allocated) {
		if (!dma_free(device.rx_ring_buffer))
			success = false;
		else
			device.rx_ring_allocated = false;
	}
	if (success) {
		device.tx_descriptors = NULL;
		device.rx_descriptors = NULL;
		device.tx_tail = 0;
		device.tx_reclaim = 0;
		device.rx_head = 0;
	}
	return success;
}

static bool
cleanup_before_bus_master(void)
{
	bool cleaned = true;

	if (device.registers != NULL) {
		(void)e1000_write(E1000_REG_RCTL, 0);
		(void)e1000_write(E1000_REG_TCTL, 0);
		if (!disable_pci_decode())
			cleaned = false;
	}
	if (cleaned && !release_dma_resources(true))
		cleaned = false;
	if (cleaned && device.mmio.generation != 0) {
		if (pci_mmio_release(&device.mmio))
			device.registers = NULL;
		else
			cleaned = false;
	}
	return cleaned;
}

static bool
stop_hardware(void)
{
	struct e1000_register_ops operations = {
		.read32 = e1000_mmio_read32,
		.write32 = e1000_mmio_write32,
		.context = (void *)device.registers
	};
	bool timed_out = false;

	device.state = E1000_STATE_STOPPING;
	if (device.registers == NULL || !e1000_write(E1000_REG_RCTL, 0) ||
	    !e1000_write(E1000_REG_TCTL, 0))
		return false;
	device.engines_enabled = false;
	/* The reset completion and PCI command readback together establish the
	 * stop condition. A timeout alone never authorizes freeing DMA memory. */
	if (!e1000_reset_and_validate(&operations, E1000_STOP_POLL_LIMIT,
	    &timed_out)) {
		device.failure = timed_out ? E1000_FAILURE_RESET_TIMEOUT :
		    E1000_FAILURE_POST_RESET;
		(void)disable_pci_decode();
		return false;
	}
	if (!disable_pci_decode()) {
		device.failure = E1000_FAILURE_PCI_CONFIG;
		return false;
	}
	return true;
}

bool
e1000_initialize(void)
{
	struct e1000_register_ops operations;
	volatile void *mapped;
	uint32_t control;
	uint32_t status;
	uint8_t mac_address[6];
	bool reset_timed_out = false;

	if (device.state == E1000_STATE_DMA_ACTIVE ||
	    device.state == E1000_STATE_INITIALIZED)
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
	struct pci_bar_resource probed_bar;
	if (!pci_bar_probe_size(&device.pci_lease, 0, &probed_bar) ||
	    probed_bar.base != device.resources.register_mmio.base ||
	    probed_bar.aperture_size != E1000_QEMU_BAR0_APERTURE) {
		device.failure = E1000_FAILURE_PROFILE;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	device.resources.register_mmio = probed_bar;
	device.state = E1000_STATE_RESOURCES_VALIDATED;
	if (!set_pci_decode_without_bus_master()) {
		device.failure = E1000_FAILURE_PCI_CONFIG;
		device.state = E1000_STATE_FAILED;
		(void)disable_pci_decode();
		return false;
	}
	if (!pci_mmio_map(&device.pci_lease, &device.resources.register_mmio, 0,
	    E1000_QEMU_BAR0_APERTURE, &device.mmio)) {
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
		.read32 = e1000_mmio_read32,
		.write32 = e1000_mmio_write32,
		.context = (void *)device.registers
	};
	device.state = E1000_STATE_RESET_IN_PROGRESS;
	if (!e1000_reset_and_validate(&operations, E1000_RESET_POLL_LIMIT,
	    &reset_timed_out)) {
		device.failure = reset_timed_out ? E1000_FAILURE_RESET_TIMEOUT :
		    E1000_FAILURE_POST_RESET;
		device.state = E1000_STATE_FAILED;
		if (disable_pci_decode() && pci_mmio_release(&device.mmio))
			device.registers = NULL;
		return false;
	}
	if (!e1000_read(E1000_REG_CTRL, &control) || control == UINT32_MAX ||
	    !e1000_read(E1000_REG_STATUS, &status) || status == UINT32_MAX) {
		device.failure = E1000_FAILURE_POST_RESET;
		device.state = E1000_STATE_FAILED;
		(void)cleanup_before_bus_master();
		return false;
	}
	if (!read_mac_address(mac_address)) {
		device.failure = E1000_FAILURE_POST_RESET;
		device.state = E1000_STATE_FAILED;
		(void)cleanup_before_bus_master();
		return false;
	}
	for (size_t i = 0; i < sizeof(mac_address); i++)
		device.network_device.mac_address[i] = mac_address[i];
	if (!allocate_rings() || !program_rings()) {
		device.failure = E1000_FAILURE_DMA_ALLOC;
		device.state = E1000_STATE_FAILED;
		(void)cleanup_before_bus_master();
		return false;
	}
	device.state = E1000_STATE_INITIALIZED;
	/* PCI Bus Mastering is the final gate: allocations are pinned, every RX
	 * descriptor is published, and all ring registers are programmed first. */
	if (!enable_pci_bus_master() || !enable_engines()) {
		device.failure = E1000_FAILURE_PCI_CONFIG;
		if (device.bus_master_enabled && stop_hardware())
			(void)release_dma_resources(true);
		else if (!device.bus_master_enabled)
			(void)cleanup_before_bus_master();
		device.state = E1000_STATE_FAILED;
		return false;
	}
	device.state = E1000_STATE_DMA_ACTIVE;
	klog("OpenSillicon: e1000 DMA rings ready TX/RX ");
	klog_u64(E1000_TX_RING_COUNT);
	klog(" descriptors each; RX buffer ");
	klog_u64(E1000_DMA_BUFFER_SIZE);
	klog("; PCI command ");
	klog_u64(device.pci_command_after);
	klog(status & E1000_STATUS_LINK_UP ? "; link up\n" : "; link down\n");
	return true;
}

static bool
reap_tx_completions(void)
{
	for (size_t n = 0; n < E1000_TX_RING_COUNT; n++) {
		uint32_t index = device.tx_reclaim;
		struct e1000_tx_descriptor *descriptor;
		struct e1000_tx_slot *slot;

		if (!device.tx[index].in_flight)
			break;
		slot = &device.tx[index];
		descriptor = &device.tx_descriptors[index];
		__asm__ volatile ("mfence" : : : "memory");
		if (!e1000_tx_completion_valid(descriptor->status))
			break;
		if (!slot->dma_owned ||
		    !dma_device_complete(slot->buffer) ||
		    !dma_ring_reclaim(&device.tx_ring, index, true) ||
		    !netbuf_transfer(slot->packet, NETBUF_OWNER_DEVICE,
		    NETBUF_OWNER_CORE)) {
			device.failure = E1000_FAILURE_REGISTER;
			device.state = E1000_STATE_FAILED;
			return false;
		}
		slot->dma_owned = false;
		slot->in_flight = false;
		device.tx_reclaim = (index + 1) % E1000_TX_RING_COUNT;
	}
	return true;
}

static int
e1000_transmit_callback(struct net_device *network_device,
	struct netbuf_ref packet)
{
	struct e1000_tx_slot *slot;
	struct e1000_tx_descriptor *descriptor;
	uint8_t *source;
	uint64_t device_address;
	size_t length;
	uint32_t hardware_head;
	uint32_t next_tail;
	uint32_t index;

	(void)network_device;
	if (device.state != E1000_STATE_DMA_ACTIVE ||
	    !reap_tx_completions())
		return NET_DEVICE_IO_ERROR;
	source = netbuf_data(packet, NETBUF_OWNER_CORE);
	length = netbuf_length(packet, NETBUF_OWNER_CORE);
	if (source == NULL || length < 14 || length > E1000_MAX_FRAME_SIZE)
		return NET_DEVICE_INVALID;
	index = device.tx_tail;
	next_tail = (index + 1) % E1000_TX_RING_COUNT;
	if (!e1000_read(E1000_REG_TDH, &hardware_head) ||
	    !e1000_tx_slot_available(index, hardware_head,
	    E1000_TX_RING_COUNT, device.tx[index].in_flight))
		return NET_DEVICE_NO_BUFFER;
	slot = &device.tx[index];
	descriptor = &device.tx_descriptors[index];
	for (size_t i = 0; i < length; i++)
		((volatile uint8_t *)slot->memory.cpu_virtual_address)[i] = source[i];
	if (!e1000_tx_descriptor_prepare(descriptor,
	    slot->memory.device_address, (uint16_t)length))
		return NET_DEVICE_INVALID;
	if (!netbuf_transfer(packet, NETBUF_OWNER_CORE, NETBUF_OWNER_DEVICE))
		return NET_DEVICE_INVALID;
	if (!dma_device_acquire(slot->buffer, &device_address) ||
	    device_address != descriptor->buffer_address) {
		(void)netbuf_transfer(packet, NETBUF_OWNER_DEVICE, NETBUF_OWNER_CORE);
		return NET_DEVICE_IO_ERROR;
	}
	slot->dma_owned = true;
	if (!dma_ring_publish(&device.tx_ring, index)) {
		(void)dma_device_complete(slot->buffer);
		slot->dma_owned = false;
		(void)netbuf_transfer(packet, NETBUF_OWNER_DEVICE, NETBUF_OWNER_CORE);
		return NET_DEVICE_IO_ERROR;
	}
	slot->packet = packet;
	slot->in_flight = true;
	device.tx_tail = next_tail;
	if (!e1000_write(E1000_REG_TDT, next_tail)) {
		/* The invalid register write means the device was not doorbelled. */
		(void)dma_device_complete(slot->buffer);
		slot->dma_owned = false;
		(void)dma_ring_reclaim(&device.tx_ring, index, true);
		slot->in_flight = false;
		device.tx_tail = index;
		(void)netbuf_transfer(packet, NETBUF_OWNER_DEVICE, NETBUF_OWNER_CORE);
		return NET_DEVICE_IO_ERROR;
	}
	return NET_DEVICE_OK;
}

bool
e1000_wait_transmit(struct netbuf_ref packet, uint32_t poll_limit)
{
	if (poll_limit == 0)
		return false;
	for (uint32_t poll = 0; poll < poll_limit; poll++) {
		if (!reap_tx_completions())
			return false;
		if (netbuf_data(packet, NETBUF_OWNER_CORE) != NULL)
			return true;
		__asm__ volatile ("pause" : : : "memory");
	}
	return false;
}

static int
e1000_wait_transmit_callback(struct net_device *network_device,
	struct netbuf_ref packet, size_t poll_limit)
{
	(void)network_device;
	return poll_limit <= UINT32_MAX && e1000_wait_transmit(packet,
	    (uint32_t)poll_limit) ? NET_DEVICE_OK : NET_DEVICE_IO_ERROR;
}

static bool
rearm_rx_descriptor(uint32_t index)
{
	struct e1000_rx_descriptor *descriptor = &device.rx_descriptors[index];
	uint64_t address;

	descriptor->length = 0;
	descriptor->checksum = 0;
	descriptor->status = 0;
	descriptor->errors = 0;
	descriptor->special = 0;
	descriptor->buffer_address = device.rx[index].memory.device_address;
	if (!dma_device_acquire(device.rx[index].buffer, &address) ||
	    address != descriptor->buffer_address)
		return false;
	device.rx[index].dma_owned = true;
	if (!dma_ring_publish(&device.rx_ring, index)) {
		(void)dma_device_complete(device.rx[index].buffer);
		device.rx[index].dma_owned = false;
		return false;
	}
	/* e1000 RDT returns this completed descriptor to the hardware ring. */
	return e1000_write(E1000_REG_RDT, index);
}

static int
e1000_receive_callback(struct net_device *network_device, size_t budget,
	struct netbuf_ref *packets, size_t capacity, size_t *count)
{
	(void)network_device;
	if (device.state != E1000_STATE_DMA_ACTIVE || packets == NULL ||
	    count == NULL || capacity == 0 || budget == 0)
		return NET_DEVICE_UNAVAILABLE;
	*count = 0;
	while (*count < budget && *count < capacity) {
		uint32_t index = device.rx_head;
		struct e1000_rx_descriptor *descriptor = &device.rx_descriptors[index];
		struct e1000_rx_slot *slot = &device.rx[index];
		uint8_t status;
		uint16_t length;
		struct netbuf_ref packet;
		uint8_t *destination;

		__asm__ volatile ("mfence" : : : "memory");
		status = descriptor->status;
		if ((status & E1000_RXD_STAT_DD) == 0)
			break;
		length = descriptor->length;
		if (!slot->dma_owned || !dma_device_complete(slot->buffer) ||
		    !dma_ring_reclaim(&device.rx_ring, index, true)) {
			device.failure = E1000_FAILURE_REGISTER;
			device.state = E1000_STATE_FAILED;
			return NET_DEVICE_IO_ERROR;
		}
		slot->dma_owned = false;
		if (e1000_rx_completion_valid(status, descriptor->errors, length) &&
		    netbuf_alloc(NETBUF_OWNER_CORE, &packet)) {
			destination = netbuf_data(packet, NETBUF_OWNER_CORE);
			if (destination == NULL || length > netbuf_capacity(packet,
			    NETBUF_OWNER_CORE)) {
				(void)netbuf_release(packet, NETBUF_OWNER_CORE);
				return NET_DEVICE_IO_ERROR;
			}
			for (size_t i = 0; i < length; i++)
				destination[i] = ((volatile uint8_t *)
				    slot->memory.cpu_virtual_address)[i];
			if (!netbuf_set_length(packet, NETBUF_OWNER_CORE, length)) {
				(void)netbuf_release(packet, NETBUF_OWNER_CORE);
				return NET_DEVICE_IO_ERROR;
			}
			packets[(*count)++] = packet;
		}
		if (!rearm_rx_descriptor(index)) {
			device.failure = E1000_FAILURE_REGISTER;
			device.state = E1000_STATE_FAILED;
			return *count == 0 ? NET_DEVICE_IO_ERROR : NET_DEVICE_OK;
		}
		device.rx_head = (index + 1) % E1000_RX_RING_COUNT;
	}
	return NET_DEVICE_OK;
}

static int
e1000_initialize_callback(struct net_device *network_device)
{
	(void)network_device;
	return e1000_initialize() ? NET_DEVICE_OK : NET_DEVICE_IO_ERROR;
}

static void
e1000_shutdown_callback(struct net_device *network_device)
{
	if (!e1000_shutdown())
		network_device->state = NET_DEVICE_FAILED;
}

static bool
e1000_link_up_callback(const struct net_device *network_device)
{
	uint32_t status;

	(void)network_device;
	return device.state == E1000_STATE_DMA_ACTIVE &&
	    e1000_read(E1000_REG_STATUS, &status) &&
	    (status & E1000_STATUS_LINK_UP) != 0;
}

bool
e1000_shutdown(void)
{
	bool stopped;

	if (device.state == E1000_STATE_STOPPED)
		return true;
	if (device.registers == NULL) {
		if (!device.bus_master_enabled && release_dma_resources(true)) {
			if (device.pci_lease.generation != 0 &&
			    !pci_device_release(&device.pci_lease))
				return false;
			device.state = E1000_STATE_STOPPED;
			return true;
		}
		return false;
	}
	stopped = !device.bus_master_enabled && !device.engines_enabled ?
	    (e1000_write(E1000_REG_RCTL, 0) && e1000_write(E1000_REG_TCTL, 0) &&
	    disable_pci_decode()) : stop_hardware();
	if (!stopped) {
		device.state = E1000_STATE_FAILED;
		return false;
	}
	if (!release_dma_resources(true)) {
		device.failure = E1000_FAILURE_DMA_ALLOC;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	if (!pci_mmio_release(&device.mmio)) {
		device.failure = E1000_FAILURE_MAP;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	if (!pci_device_release(&device.pci_lease)) {
		device.failure = E1000_FAILURE_PCI_CONFIG;
		device.state = E1000_STATE_FAILED;
		return false;
	}
	device.registers = NULL;
	device.state = E1000_STATE_STOPPED;
	device.network_device.state = NET_DEVICE_DOWN;
	klog("OpenSillicon: e1000 DMA stopped, bus mastering disabled, rings released\n");
	return true;
}


struct net_device *
e1000_network_device(void)
{
	return device.discovered ? &device.network_device : NULL;
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
