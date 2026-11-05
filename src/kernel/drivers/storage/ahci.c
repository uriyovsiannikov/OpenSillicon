#include <string.h>

#include "arch/x86_64/ahci.h"
#include "kernel/errno.h"
#include "kernel/dma.h"
#include "kernel/pmm.h"
#include "kernel/log.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096ULL
#define AHCI_TIMEOUT 10000000U
#define AHCI_CAP 0x00U
#define AHCI_GHC 0x04U
#define AHCI_PI 0x0cU
#define AHCI_PORTS 0x100U
#define AHCI_MMIO_SIZE (AHCI_PORTS + 32U * 0x80U)
#define PORT_CLB 0x00U
#define PORT_FB 0x08U
#define PORT_IS 0x10U
#define PORT_CMD 0x18U
#define PORT_TFD 0x20U
#define PORT_SIG 0x24U
#define PORT_SSTS 0x28U
#define PORT_CI 0x38U
#define PORT_CMD_ST 1U
#define PORT_CMD_FRE (1U << 4)
#define PORT_CMD_FR (1U << 14)
#define PORT_CMD_CR (1U << 15)
#define PORT_TFD_BSY 0x80U
#define PORT_TFD_DRQ 0x08U
#define AHCI_CMD_READ_DMA_EXT 0x25U
#define AHCI_CMD_WRITE_DMA_EXT 0x35U
#define AHCI_CMD_IDENTIFY 0xecU
#define AHCI_CMD_FLUSH_CACHE_EXT 0xeaU
#define AHCI_CMD_FLUSH_CACHE 0xe7U

struct ahci_command_header {
	uint16_t flags;
	uint16_t prdt_length;
	uint32_t transferred;
	uint32_t table_low;
	uint32_t table_high;
	uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt {
	uint32_t data_low;
	uint32_t data_high;
	uint32_t reserved;
	uint32_t byte_count_flags;
} __attribute__((packed));

struct ahci_command_table {
	uint8_t command_fis[64];
	uint8_t atapi_command[16];
	uint8_t reserved[48];
	struct ahci_prdt prdt[1];
} __attribute__((packed, aligned(128)));

struct ahci_state {
	volatile uint32_t *registers;
	volatile uint32_t *port;
	struct ahci_command_header *headers;
	struct ahci_command_table *table;
	uint8_t *dma_buffer;
	uint64_t buffer_physical;
	uint32_t sector_size;
	uint64_t sector_count;
	bool lba48;
	bool supports_flush;
	bool supports_flush_ext;
	bool initialized;
};

static struct ahci_state state;
static struct spinlock ahci_command_lock = SPINLOCK_INITIALIZER;

static uint32_t
mmio_read(volatile uint32_t *base, uint32_t offset)
{
	return base[offset / 4];
}

static void
mmio_write(volatile uint32_t *base, uint32_t offset, uint32_t value)
{
	base[offset / 4] = value;
}

static bool
dma_address_valid(uint64_t address, bool address64)
{
	return address64 || address <= UINT32_MAX;
}

static bool
wait_port_clear(uint32_t mask)
{
	uint32_t i;

	for (i = 0; i < AHCI_TIMEOUT; i++)
		if ((mmio_read(state.port, PORT_TFD) & mask) == 0)
			return true;
	return false;
}

static int
issue_command(uint8_t command, uint64_t lba, uint16_t sectors,
	bool write, bool data_command)
{
	uint8_t *fis = state.table->command_fis;
	uint32_t i;

	if (!wait_port_clear(PORT_TFD_BSY | PORT_TFD_DRQ))
		return -ETIMEDOUT;
	memset(fis, 0, sizeof(state.table->command_fis));
	fis[0] = 0x27;
	fis[1] = 1U << 7;
	fis[2] = command;
	if (command == AHCI_CMD_READ_DMA_EXT ||
	    command == AHCI_CMD_WRITE_DMA_EXT) {
		fis[4] = (uint8_t)lba;
		fis[5] = (uint8_t)(lba >> 8);
		fis[6] = (uint8_t)(lba >> 16);
		fis[7] = 1U << 6;
		fis[8] = (uint8_t)(lba >> 24);
		fis[9] = (uint8_t)(lba >> 32);
		fis[10] = (uint8_t)(lba >> 40);
		fis[12] = (uint8_t)sectors;
		fis[13] = (uint8_t)(sectors >> 8);
	}
	state.headers[0].flags = (uint16_t)(5U | (write ? (1U << 6) : 0));
	state.headers[0].prdt_length = data_command ? 1 : 0;
	state.headers[0].transferred = 0;
	state.table->prdt[0].data_low = (uint32_t)state.buffer_physical;
	state.table->prdt[0].data_high = (uint32_t)(state.buffer_physical >> 32);
	state.table->prdt[0].reserved = 0;
	state.table->prdt[0].byte_count_flags =
	    data_command ? (state.sector_size - 1U) | (1U << 31) : 0;
	mmio_write(state.port, PORT_IS, UINT32_MAX);
	mmio_write(state.port, PORT_CI, 1U);
	for (i = 0; i < AHCI_TIMEOUT; i++) {
		uint32_t interrupt_status = mmio_read(state.port, PORT_IS);
		if ((interrupt_status & (1U << 30)) != 0)
			return -EIO;
		if ((mmio_read(state.port, PORT_CI) & 1U) == 0)
			return 0;
	}
	return -ETIMEDOUT;
}

static int
ahci_transfer(uint64_t lba, uint32_t count, void *buffer, bool write)
{
	uint32_t i;
	uint8_t *bytes = buffer;
	uint64_t flags = spinlock_lock_irqsave(&ahci_command_lock);

	for (i = 0; i < count; i++) {
		int error;

		if (write)
			memcpy(state.dma_buffer, bytes + (size_t)i * state.sector_size,
			    state.sector_size);
		error = issue_command(write ? AHCI_CMD_WRITE_DMA_EXT :
		    AHCI_CMD_READ_DMA_EXT, lba + i, 1, write, true);
		if (error != 0) {
			spinlock_unlock_irqrestore(&ahci_command_lock, flags);
			return error;
		}
		if (!write)
			memcpy(bytes + (size_t)i * state.sector_size, state.dma_buffer,
			    state.sector_size);
	}
	spinlock_unlock_irqrestore(&ahci_command_lock, flags);
	return 0;
}

static int
device_read(struct block_device *device, uint64_t lba, uint32_t count,
	void *buffer)
{
	(void)device;
	return ahci_transfer(lba, count, buffer, false);
}

static int
device_write(struct block_device *device, uint64_t lba, uint32_t count,
	const void *buffer)
{
	(void)device;
	return ahci_transfer(lba, count, (void *)buffer, true);
}

static int
device_flush(struct block_device *device)
{
	uint64_t flags;
	int error;

	(void)device;
	if (!state.supports_flush)
		return -EOPNOTSUPP;
	flags = spinlock_lock_irqsave(&ahci_command_lock);
	error = issue_command(state.supports_flush_ext ? AHCI_CMD_FLUSH_CACHE_EXT :
	    AHCI_CMD_FLUSH_CACHE, 0, 0, false, false);
	spinlock_unlock_irqrestore(&ahci_command_lock, flags);
	return error;
}

static const struct block_device_operations operations = {
	.read = device_read,
	.write = device_write,
	.flush = device_flush
};

static bool
allocate_dma(uint64_t *physical, void **virtual_address, bool address64)
{
	if (!pmm_alloc_page(physical))
		return false;
	if (!dma_address_valid(*physical, address64) ||
	    vmm_hhdm_offset() > UINT64_MAX - *physical ||
	    PAGE_SIZE - 1 > UINT64_MAX - (vmm_hhdm_offset() + *physical)) {
		(void)pmm_free_page(*physical);
		return false;
	}
	*virtual_address = (void *)(uintptr_t)(vmm_hhdm_offset() + *physical);
	memset(*virtual_address, 0, PAGE_SIZE);
	return true;
}

static bool
allocate_command_dma(uint64_t physical[4], void *virtual_address[4],
	bool address64)
{
	for (size_t i = 0; i < 4; i++) {
		if (allocate_dma(&physical[i], &virtual_address[i], address64))
			continue;
		while (i != 0) {
			i--;
			if (!pmm_free_page(physical[i])) {
				klog("AHCI DMA allocation rollback failed\n");
				kernel_halt();
			}
		}
		return false;
	}
	return true;
}

bool
ahci_initialize(const struct pci_device_info *controller,
	struct block_device *device)
{
	uint32_t cap;
	uint32_t ports;
	uint32_t port_index;
	uint64_t bar;
	uint64_t virtual_base;
	uint64_t bar_offset;
	size_t mmio_page_count;
	size_t mapped_pages = 0;
	uint16_t identify[256];
	bool address64;
	uint32_t i;
	int error;

	if (controller == NULL || device == NULL || state.initialized ||
	    !dma_identity_addressing_available() ||
	    controller->class_code != 1 || controller->subclass != 6 ||
	    controller->programming_interface != 1)
		return false;
	bar = controller->bars[5] & ~0xfULL;
	if (bar == 0 || (controller->bars[5] & 1U) != 0 ||
	    (controller->bars[5] & 0x6U) != 0)
		return false;
	bar_offset = bar & (PAGE_SIZE - 1);
	virtual_base = (vmm_vmalloc_start() + 0x300000ULL) & ~(PAGE_SIZE - 1);
	mmio_page_count = (size_t)((bar_offset + AHCI_MMIO_SIZE + PAGE_SIZE - 1) /
	    PAGE_SIZE);
	if (virtual_base >= vmm_vmalloc_end() ||
	    mmio_page_count > (vmm_vmalloc_end() - virtual_base) / PAGE_SIZE ||
	    bar - bar_offset > UINT64_MAX -
	    (mmio_page_count - 1) * PAGE_SIZE ||
	    !vmm_physical_range_mmio_safe(bar - bar_offset,
	    mmio_page_count * PAGE_SIZE))
		return false;
	while (mapped_pages < mmio_page_count) {
		if (!vmm_map_page(vmm_kernel_address_space(),
		    virtual_base + mapped_pages * PAGE_SIZE,
		    bar - bar_offset + mapped_pages * PAGE_SIZE,
		    VMM_WRITE | VMM_UNCACHED)) {
			while (mapped_pages != 0) {
				uint64_t physical;
				mapped_pages--;
				if (!vmm_unmap_page(vmm_kernel_address_space(),
				    virtual_base + mapped_pages * PAGE_SIZE, &physical)) {
					klog("AHCI MMIO mapping rollback failed\n");
					kernel_halt();
				}
			}
			return false;
		}
		mapped_pages++;
	}
	state.registers = (volatile uint32_t *)(uintptr_t)(virtual_base + bar_offset);
	/* The PCI command register is changed only for this AHCI function. */
	if (!pci_config_write(controller->segment, controller->bus,
	    controller->device, controller->function, 0x04, 16,
	    (uint16_t)(controller->command | 0x0006U))) {
		klog("AHCI PCI command write failed; command ");
		klog_u64(controller->command);
		klog("\n");
		while (mapped_pages != 0) {
			uint64_t physical;
			mapped_pages--;
			if (!vmm_unmap_page(vmm_kernel_address_space(),
			    virtual_base + mapped_pages * PAGE_SIZE, &physical) ||
			    physical != bar - bar_offset + mapped_pages * PAGE_SIZE) {
				klog("AHCI MMIO mapping rollback failed\n");
				kernel_halt();
			}
		}
		state.registers = NULL;
		return false;
	}
	mmio_write(state.registers, AHCI_GHC,
	    mmio_read(state.registers, AHCI_GHC) | (1U << 31));
	cap = mmio_read(state.registers, AHCI_CAP);
	ports = mmio_read(state.registers, AHCI_PI);
	address64 = (cap & (1U << 31)) != 0;
	for (port_index = 0; port_index < 32; port_index++) {
		volatile uint32_t *port;
		uint32_t ssts;
		uint64_t dma_physical[4];
		void *dma_virtual[4];
		uint64_t headers_phys, fis_phys, table_phys, buffer_phys;
		void *headers_virt, *fis_virt, *table_virt, *buffer_virt;

		if ((ports & (1U << port_index)) == 0)
			continue;
		port = (volatile uint32_t *)((uintptr_t)state.registers +
		    AHCI_PORTS + port_index * 0x80U);
		state.port = port;
		ssts = mmio_read(port, PORT_SSTS);
		if ((ssts & 0xfU) != 3 || ((ssts >> 8) & 0xfU) != 1 ||
		    mmio_read(port, PORT_SIG) != 0x00000101U)
			continue;
		mmio_write(port, PORT_CMD, mmio_read(port, PORT_CMD) & ~PORT_CMD_ST);
		for (i = 0; i < AHCI_TIMEOUT &&
		    (mmio_read(port, PORT_CMD) & PORT_CMD_CR) != 0; i++)
			;
		if (i == AHCI_TIMEOUT)
			return false;
		mmio_write(port, PORT_CMD, mmio_read(port, PORT_CMD) & ~PORT_CMD_FRE);
		for (i = 0; i < AHCI_TIMEOUT &&
		    (mmio_read(port, PORT_CMD) & PORT_CMD_FR) != 0; i++)
			;
		if (i == AHCI_TIMEOUT ||
		    !allocate_command_dma(dma_physical, dma_virtual, address64))
			return false;
		headers_phys = dma_physical[0];
		fis_phys = dma_physical[1];
		table_phys = dma_physical[2];
		buffer_phys = dma_physical[3];
		headers_virt = dma_virtual[0];
		fis_virt = dma_virtual[1];
		table_virt = dma_virtual[2];
		buffer_virt = dma_virtual[3];
		state.buffer_physical = buffer_phys;
		state.headers = headers_virt;
		(void)fis_virt;
		state.table = table_virt;
		state.dma_buffer = buffer_virt;
		state.headers[0].table_low = (uint32_t)table_phys;
		state.headers[0].table_high = (uint32_t)(table_phys >> 32);
		mmio_write(port, PORT_CLB, (uint32_t)headers_phys);
		mmio_write(port, PORT_CLB + 4, (uint32_t)(headers_phys >> 32));
		mmio_write(port, PORT_FB, (uint32_t)fis_phys);
		mmio_write(port, PORT_FB + 4, (uint32_t)(fis_phys >> 32));
		mmio_write(port, PORT_CMD, mmio_read(port, PORT_CMD) | PORT_CMD_FRE);
		mmio_write(port, PORT_CMD, mmio_read(port, PORT_CMD) | PORT_CMD_ST);
		state.sector_size = 512;
		if (issue_command(AHCI_CMD_IDENTIFY, 0, 0, false, true) != 0)
			continue;
		memcpy(identify, state.dma_buffer, sizeof(identify));
		state.lba48 = (identify[83] & (1U << 10)) != 0;
		state.sector_count = state.lba48 ?
		    ((uint64_t)identify[100] | (uint64_t)identify[101] << 16 |
		    (uint64_t)identify[102] << 32 | (uint64_t)identify[103] << 48) :
		    ((uint32_t)identify[60] | (uint32_t)identify[61] << 16);
		if (state.sector_count == 0 || !state.lba48)
			continue;
		if ((identify[106] & 0xc000U) == 0x4000U &&
		    (identify[106] & (1U << 12)) != 0) {
			uint32_t words = (uint32_t)identify[117] |
			    (uint32_t)identify[118] << 16;
			if (words > UINT32_MAX / 2)
				continue;
			state.sector_size = words * 2;
		}
		if (state.sector_size != 512 && state.sector_size != 4096)
			continue;
		state.supports_flush = (identify[82] & (1U << 12)) != 0 ||
		    (identify[83] & (1U << 13)) != 0;
		state.supports_flush_ext = (identify[83] & (1U << 13)) != 0;
		*device = (struct block_device){
			.name = "ahci0",
			.logical_sector_size = state.sector_size,
			.sector_count = state.sector_count,
			.operations = &operations
		};
		state.initialized = true;
		error = block_device_register(device);
		if (error != 0) {
			state.initialized = false;
			return false;
		}
		return true;
	}
	return false;
}
