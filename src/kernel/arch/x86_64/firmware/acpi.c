#include <string.h>

#include "arch/x86_64/acpi.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/spinlock.h"
#include "kernel/timer.h"
#include "kernel/vmm.h"
#include "acpi.h"
#include "acpiosxf.h"
#include "acpixf.h"

#define ACPI_EINVAL 22
#define MADT_HEADER_SIZE 44U
#define MADT_LOCAL_APIC 0U
#define MADT_IO_APIC 1U
#define MADT_INTERRUPT_OVERRIDE 2U
#define MADT_NMI_SOURCE 3U
#define MADT_LOCAL_APIC_NMI 4U
#define MADT_LOCAL_APIC_ADDRESS_OVERRIDE 5U
#define MADT_LOCAL_X2APIC 9U
#define MCFG_HEADER_SIZE 44U
#define PAGE_SIZE 4096ULL
#define ACPI_MAP_BASE (2ULL * 1024ULL * 1024ULL)
#define ACPI_MAP_SLOTS 128U

struct acpi_mapping {
	uint64_t virtual_base;
	uint64_t physical_base;
	size_t page_count;
	bool active;
};

static uint64_t rsdp_physical;
static struct acpi_mcfg_region mcfg_cache[ACPI_MCFG_MAX_REGIONS];
static size_t mcfg_count;
static struct acpi_mapping acpi_mappings[ACPI_MAP_SLOTS];
static uint64_t acpi_map_next;
static struct spinlock acpi_mapping_lock = SPINLOCK_INITIALIZER;

static uint16_t
read_u16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static uint32_t
read_u32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t
read_u64(const uint8_t *bytes)
{
	return (uint64_t)read_u32(bytes) | ((uint64_t)read_u32(bytes + 4) << 32);
}

static bool
checksum_valid(const uint8_t *bytes, size_t length)
{
	uint8_t sum = 0;
	size_t i;

	for (i = 0; i < length; i++)
		sum = (uint8_t)(sum + bytes[i]);
	return sum == 0;
}

int
acpi_parse_madt(const void *table, size_t table_size,
	struct acpi_madt_info *info)
{
	const uint8_t *bytes = table;
	struct acpi_madt_info parsed = {0};
	uint32_t length;
	size_t offset;

	if (table == NULL || info == NULL || table_size < MADT_HEADER_SIZE ||
	    memcmp(bytes, "APIC", 4) != 0)
		return -ACPI_EINVAL;
	length = read_u32(bytes + 4);
	if (length < MADT_HEADER_SIZE || length > table_size ||
	    !checksum_valid(bytes, length))
		return -ACPI_EINVAL;
	parsed.lapic_address = read_u32(bytes + 36);
	parsed.flags = read_u32(bytes + 40);
	if (parsed.lapic_address == 0)
		return -ACPI_EINVAL;
	for (offset = MADT_HEADER_SIZE; offset < length;) {
		uint8_t type;
		uint8_t record_length;
		const uint8_t *record;

		if (length - offset < 2)
			return -ACPI_EINVAL;
		record = bytes + offset;
		type = record[0];
		record_length = record[1];
		if (record_length < 2 || record_length > length - offset)
			return -ACPI_EINVAL;
		switch (type) {
		case MADT_LOCAL_APIC:
			if (record_length != 8)
				return -ACPI_EINVAL;
			break;
		case MADT_IO_APIC:
			if (record_length != 12 || parsed.ioapic_count ==
			    ACPI_MADT_MAX_IOAPICS || read_u32(record + 4) == 0)
				return -ACPI_EINVAL;
			parsed.ioapics[parsed.ioapic_count++] =
			    (struct acpi_madt_ioapic){
				.id = record[2],
				.address = read_u32(record + 4),
				.gsi_base = read_u32(record + 8)
			};
			break;
		case MADT_INTERRUPT_OVERRIDE:
			if (record_length != 10 || parsed.override_count ==
			    ACPI_MADT_MAX_OVERRIDES || record[2] != 0)
				return -ACPI_EINVAL;
			parsed.overrides[parsed.override_count++] =
			    (struct acpi_madt_override){
				.source = record[3],
				.gsi = read_u32(record + 4),
				.flags = read_u16(record + 8)
			};
			break;
		case MADT_NMI_SOURCE:
			if (record_length != 8 || parsed.nmi_count ==
			    ACPI_MADT_MAX_NMIS)
				return -ACPI_EINVAL;
			parsed.nmis[parsed.nmi_count++] = (struct acpi_madt_nmi){
				.flags = read_u16(record + 2),
				.gsi = read_u32(record + 4)
			};
			break;
		case MADT_LOCAL_APIC_NMI:
			if (record_length != 6)
				return -ACPI_EINVAL;
			break;
		case MADT_LOCAL_APIC_ADDRESS_OVERRIDE:
			if (record_length != 12 || read_u64(record + 4) == 0)
				return -ACPI_EINVAL;
			parsed.lapic_address = read_u64(record + 4);
			break;
		case MADT_LOCAL_X2APIC:
			return -ACPI_EINVAL;
		default:
			break;
		}
		offset += record_length;
	}
	if (parsed.ioapic_count == 0)
		return -ACPI_EINVAL;
	*info = parsed;
	return 0;
}

int
acpi_parse_mcfg(const void *table, size_t table_size,
	struct acpi_mcfg_region *regions, size_t capacity, size_t *count)
{
	const uint8_t *bytes = table;
	uint32_t length;
	size_t region_count;
	size_t i;

	if (table == NULL || count == NULL || table_size < MCFG_HEADER_SIZE ||
	    memcmp(bytes, "MCFG", 4) != 0)
		return -ACPI_EINVAL;
	length = read_u32(bytes + 4);
	if (length < MCFG_HEADER_SIZE || length > table_size ||
	    ((length - MCFG_HEADER_SIZE) % 16) != 0 ||
	    !checksum_valid(bytes, length))
		return -ACPI_EINVAL;
	region_count = (length - MCFG_HEADER_SIZE) / 16;
	if (region_count == 0 || region_count > capacity || regions == NULL)
		return -ACPI_EINVAL;
	for (i = 0; i < region_count; i++) {
		const uint8_t *entry = bytes + MCFG_HEADER_SIZE + i * 16;
		struct acpi_mcfg_region region = {
			.base = read_u64(entry),
			.segment = read_u16(entry + 8),
			.start_bus = entry[10],
			.end_bus = entry[11]
		};
		uint64_t buses = (uint64_t)region.end_bus - region.start_bus + 1;
		uint64_t span = buses << 20;
		size_t previous;

		if (read_u32(entry + 12) != 0 || region.base == 0 ||
		    (region.base & ((1ULL << 20) - 1)) != 0 ||
		    region.end_bus < region.start_bus || region.base > UINT64_MAX - span)
			return -ACPI_EINVAL;
		for (previous = 0; previous < i; previous++) {
			if (regions[previous].segment == region.segment &&
			    region.start_bus <= regions[previous].end_bus &&
			    regions[previous].start_bus <= region.end_bus)
				return -ACPI_EINVAL;
		}
		regions[i] = region;
	}
	*count = region_count;
	return 0;
}

size_t
acpi_mcfg_regions(struct acpi_mcfg_region *regions, size_t capacity)
{
	size_t count = mcfg_count < capacity ? mcfg_count : capacity;
	size_t i;

	if (regions == NULL)
		return 0;
	for (i = 0; i < count; i++)
		regions[i] = mcfg_cache[i];
	return count;
}

bool
acpi_init(uint64_t root_pointer, struct acpi_madt_info *info)
{
	ACPI_TABLE_HEADER *table = NULL;
	struct acpi_madt_info parsed;
	ACPI_STATUS status;
	size_t count;

	if (root_pointer == 0 || info == NULL || rsdp_physical != 0) {
		klog("ACPI: invalid initialization: root ");
		klog_u64(root_pointer);
		klog(" previous root ");
		klog_u64(rsdp_physical);
		klog("\n");
		return false;
	}
	rsdp_physical = root_pointer;
	acpi_map_next = vmm_vmalloc_start() + ACPI_MAP_BASE;
	status = AcpiInitializeSubsystem();
	if (ACPI_FAILURE(status)) {
		klog("ACPI: AcpiInitializeSubsystem failed: ");
		klog_u64(status);
		klog("\n");
		return false;
	}
	status = AcpiInitializeTables(NULL, 16, FALSE);
	if (ACPI_FAILURE(status)) {
		klog("ACPI: AcpiInitializeTables failed: ");
		klog_u64(status);
		klog("\n");
		return false;
	}
	status = AcpiLoadTables();
	if (ACPI_FAILURE(status)) {
		klog("ACPI: AcpiLoadTables failed: ");
		klog_u64(status);
		klog("\n");
		return false;
	}
	status = AcpiGetTable("APIC", 1, &table);
	if (ACPI_FAILURE(status) || table == NULL ||
	    acpi_parse_madt(table, table->Length, &parsed) != 0) {
		klog("ACPI: APIC lookup/parse failed: ");
		klog_u64(status);
		klog("\n");
		return false;
	}
	if (AcpiGetTable("MCFG", 1, &table) == AE_OK && table != NULL) {
		if (acpi_parse_mcfg(table, table->Length, mcfg_cache,
		    ACPI_MCFG_MAX_REGIONS, &count) != 0) {
			klog("ACPI: invalid MCFG\n");
			return false;
		}
		mcfg_count = count;
	}
	*info = parsed;
	return true;
}

ACPI_STATUS
AcpiOsInitialize(void)
{
	return AE_OK;
}

ACPI_STATUS
AcpiOsTerminate(void)
{
	return AE_OK;
}

ACPI_PHYSICAL_ADDRESS
AcpiOsGetRootPointer(void)
{
	return rsdp_physical;
}

ACPI_STATUS
AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES *init_value,
	ACPI_STRING *new_value)
{
	(void)init_value;
	if (new_value == NULL)
		return AE_BAD_PARAMETER;
	*new_value = NULL;
	return AE_OK;
}

ACPI_STATUS
AcpiOsTableOverride(ACPI_TABLE_HEADER *existing, ACPI_TABLE_HEADER **new_table)
{
	(void)existing;
	if (new_table == NULL)
		return AE_BAD_PARAMETER;
	*new_table = NULL;
	return AE_OK;
}

ACPI_STATUS
AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER *existing,
	ACPI_PHYSICAL_ADDRESS *new_address, UINT32 *new_length)
{
	(void)existing;
	if (new_address == NULL || new_length == NULL)
		return AE_BAD_PARAMETER;
	*new_address = 0;
	*new_length = 0;
	return AE_OK;
}

struct acpi_os_lock {
	volatile uint32_t held;
};

static ACPI_CPU_FLAGS
irq_save(void)
{
	ACPI_CPU_FLAGS flags;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
}

static void
irq_restore(ACPI_CPU_FLAGS flags)
{
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
}

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK *out)
{
	struct acpi_os_lock *lock;

	if (out == NULL)
		return AE_BAD_PARAMETER;
	lock = kheap_alloc(sizeof(*lock));
	if (lock == NULL)
		return AE_NO_MEMORY;
	lock->held = 0;
	*out = lock;
	return AE_OK;
}

void
AcpiOsDeleteLock(ACPI_SPINLOCK lock)
{
	if (lock != NULL)
		(void)kheap_free(lock);
}

ACPI_CPU_FLAGS
AcpiOsAcquireLock(ACPI_SPINLOCK handle)
{
	struct acpi_os_lock *lock = handle;
	ACPI_CPU_FLAGS flags = irq_save();

	if (lock != NULL) {
		while (__atomic_exchange_n(&lock->held, 1, __ATOMIC_ACQUIRE) != 0)
			__asm__ volatile ("pause");
	}
	return flags;
}

void
AcpiOsReleaseLock(ACPI_SPINLOCK handle, ACPI_CPU_FLAGS flags)
{
	struct acpi_os_lock *lock = handle;

	if (lock != NULL)
		__atomic_store_n(&lock->held, 0, __ATOMIC_RELEASE);
	irq_restore(flags);
}

struct acpi_os_semaphore {
	struct spinlock lock;
	uint32_t count;
	uint32_t maximum;
};

ACPI_STATUS
AcpiOsCreateSemaphore(UINT32 maximum, UINT32 initial, ACPI_SEMAPHORE *out)
{
	struct acpi_os_semaphore *semaphore;

	if (out == NULL || maximum == 0 || initial > maximum)
		return AE_BAD_PARAMETER;
	semaphore = kheap_alloc(sizeof(*semaphore));
	if (semaphore == NULL)
		return AE_NO_MEMORY;
	*semaphore = (struct acpi_os_semaphore){
		.lock = SPINLOCK_INITIALIZER,
		.count = initial,
		.maximum = maximum
	};
	*out = semaphore;
	return AE_OK;
}

ACPI_STATUS
AcpiOsDeleteSemaphore(ACPI_SEMAPHORE handle)
{
	return handle == NULL || !kheap_free(handle) ? AE_BAD_PARAMETER : AE_OK;
}

ACPI_STATUS
AcpiOsWaitSemaphore(ACPI_SEMAPHORE handle, UINT32 units, UINT16 timeout)
{
	struct acpi_os_semaphore *semaphore = handle;
	uint64_t deadline;

	if (semaphore == NULL || units == 0 || units > semaphore->maximum)
		return AE_BAD_PARAMETER;
	deadline = timeout == ACPI_WAIT_FOREVER ? UINT64_MAX :
	    timer_ticks() + ((uint64_t)timeout + 9) / 10;
	for (;;) {
		uint64_t flags = spinlock_lock_irqsave(&semaphore->lock);
		if (semaphore->count >= units) {
			semaphore->count -= units;
			spinlock_unlock_irqrestore(&semaphore->lock, flags);
			return AE_OK;
		}
		spinlock_unlock_irqrestore(&semaphore->lock, flags);
		if (timeout == 0 || timer_ticks() >= deadline)
			return AE_TIME;
		__asm__ volatile ("pause");
	}
}

ACPI_STATUS
AcpiOsSignalSemaphore(ACPI_SEMAPHORE handle, UINT32 units)
{
	struct acpi_os_semaphore *semaphore = handle;
	ACPI_CPU_FLAGS flags;

	if (semaphore == NULL || units == 0)
		return AE_BAD_PARAMETER;
	flags = spinlock_lock_irqsave(&semaphore->lock);
	if (units > semaphore->maximum - semaphore->count) {
		spinlock_unlock_irqrestore(&semaphore->lock, flags);
		return AE_LIMIT;
	}
	semaphore->count += units;
	spinlock_unlock_irqrestore(&semaphore->lock, flags);
	return AE_OK;
}

ACPI_STATUS
AcpiOsCreateMutex(ACPI_MUTEX *out)
{
	return AcpiOsCreateSemaphore(1, 1, out);
}

void
AcpiOsDeleteMutex(ACPI_MUTEX mutex)
{
	(void)AcpiOsDeleteSemaphore(mutex);
}

ACPI_STATUS
AcpiOsAcquireMutex(ACPI_MUTEX mutex, UINT16 timeout)
{
	return AcpiOsWaitSemaphore(mutex, 1, timeout);
}

void
AcpiOsReleaseMutex(ACPI_MUTEX mutex)
{
	(void)AcpiOsSignalSemaphore(mutex, 1);
}

void *
AcpiOsAllocate(ACPI_SIZE size)
{
	return size == 0 ? NULL : kheap_alloc(size);
}

void
AcpiOsFree(void *memory)
{
	if (memory != NULL)
		(void)kheap_free(memory);
}

void *
AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS where, ACPI_SIZE length)
{
	uint64_t physical_base = where & ~(PAGE_SIZE - 1);
	uint64_t offset = where - physical_base;
	uint64_t span;
	uint64_t page_count;
	uint64_t virtual_base;
	size_t slot;
	size_t mapped = 0;

	if (length == 0 || length > UINT64_MAX - offset - (PAGE_SIZE - 1))
		return NULL;
	span = (offset + length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	page_count = span / PAGE_SIZE;
	if (physical_base > UINT64_MAX - span || page_count > SIZE_MAX)
		return NULL;
	uint64_t flags = spinlock_lock_irqsave(&acpi_mapping_lock);
	for (slot = 0; slot < ACPI_MAP_SLOTS; slot++) {
		if (!acpi_mappings[slot].active)
			break;
	}
	if (slot == ACPI_MAP_SLOTS || acpi_map_next == 0 ||
	    acpi_map_next > vmm_vmalloc_device_start() ||
	    span > vmm_vmalloc_device_start() - acpi_map_next) {
		spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
		return NULL;
	}
	virtual_base = acpi_map_next;
	for (mapped = 0; mapped < page_count; mapped++) {
		if (!vmm_map_page(vmm_kernel_address_space(),
		    virtual_base + mapped * PAGE_SIZE,
		    physical_base + mapped * PAGE_SIZE, VMM_WRITE))
			break;
	}
	if (mapped != page_count) {
		while (mapped != 0) {
			uint64_t ignored;
			mapped--;
			(void)vmm_unmap_page(vmm_kernel_address_space(),
			    virtual_base + mapped * PAGE_SIZE, &ignored);
		}
		spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
		return NULL;
	}
	acpi_mappings[slot] = (struct acpi_mapping){
		.virtual_base = virtual_base,
		.physical_base = physical_base,
		.page_count = page_count,
		.active = true
	};
	acpi_map_next += span;
	spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
	return (void *)(uintptr_t)(virtual_base + offset);
}

void
AcpiOsUnmapMemory(void *logical, ACPI_SIZE length)
{
	uintptr_t address = (uintptr_t)logical;
	size_t slot;
	uint64_t flags = spinlock_lock_irqsave(&acpi_mapping_lock);

	(void)length;
	for (slot = 0; slot < ACPI_MAP_SLOTS; slot++) {
		struct acpi_mapping *mapping = &acpi_mappings[slot];
		uint64_t size;

		if (!mapping->active)
			continue;
		size = mapping->page_count * PAGE_SIZE;
		if (address < mapping->virtual_base ||
		    address - mapping->virtual_base >= size)
			continue;
		for (size_t page = 0; page < mapping->page_count; page++) {
			uint64_t ignored;
			(void)vmm_unmap_page(vmm_kernel_address_space(),
			    mapping->virtual_base + page * PAGE_SIZE, &ignored);
		}
		mapping->active = false;
		if (mapping->virtual_base + size == acpi_map_next) {
			/* Reclaim only space above the highest live mapping. Walking
			 * backwards through active mappings would reuse their addresses. */
			acpi_map_next = vmm_vmalloc_start() + ACPI_MAP_BASE;
			for (size_t previous = 0; previous < ACPI_MAP_SLOTS; previous++) {
				const struct acpi_mapping *candidate = &acpi_mappings[previous];
				uint64_t end = candidate->virtual_base +
				    candidate->page_count * PAGE_SIZE;
				if (candidate->active && end > acpi_map_next)
					acpi_map_next = end;
			}
		}
		spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
		return;
	}
	spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
}

ACPI_STATUS
AcpiOsGetPhysicalAddress(void *logical, ACPI_PHYSICAL_ADDRESS *physical)
{
	uintptr_t address = (uintptr_t)logical;
	uint64_t hhdm = vmm_hhdm_offset();
	size_t slot;
	uint64_t flags;

	if (logical == NULL || physical == NULL)
		return AE_BAD_PARAMETER;
	flags = spinlock_lock_irqsave(&acpi_mapping_lock);
	for (slot = 0; slot < ACPI_MAP_SLOTS; slot++) {
		const struct acpi_mapping *mapping = &acpi_mappings[slot];
		uint64_t size;

		if (!mapping->active)
			continue;
		size = mapping->page_count * PAGE_SIZE;
		if (address >= mapping->virtual_base &&
		    address - mapping->virtual_base < size) {
			*physical = mapping->physical_base +
			    (address - mapping->virtual_base);
			spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
			return AE_OK;
		}
	}
	spinlock_unlock_irqrestore(&acpi_mapping_lock, flags);
	if (address < hhdm)
		return AE_BAD_PARAMETER;
	*physical = address - hhdm;
	return AE_OK;
}

ACPI_STATUS
AcpiOsInstallInterruptHandler(UINT32 number, ACPI_OSD_HANDLER handler,
	void *context)
{
	(void)number;
	(void)handler;
	(void)context;
	return AE_SUPPORT;
}

ACPI_STATUS
AcpiOsRemoveInterruptHandler(UINT32 number, ACPI_OSD_HANDLER handler)
{
	(void)number;
	(void)handler;
	return AE_SUPPORT;
}

ACPI_THREAD_ID
AcpiOsGetThreadId(void)
{
	return 1;
}

ACPI_STATUS
AcpiOsExecute(ACPI_EXECUTE_TYPE type, ACPI_OSD_EXEC_CALLBACK function,
	void *context)
{
	(void)type;
	(void)function;
	(void)context;
	return AE_SUPPORT;
}

void
AcpiOsWaitEventsComplete(void)
{
}

void
AcpiOsSleep(UINT64 milliseconds)
{
	uint64_t end = timer_ticks() + (milliseconds + 9) / 10;

	while (timer_ticks() < end)
		__asm__ volatile ("pause");
}

void
AcpiOsStall(UINT32 microseconds)
{
	volatile uint32_t loops = microseconds > UINT32_MAX / 64 ?
	    UINT32_MAX : microseconds * 64;

	while (loops-- != 0)
		__asm__ volatile ("pause");
}

ACPI_STATUS
AcpiOsReadPort(ACPI_IO_ADDRESS address, UINT32 *value, UINT32 width)
{
	uint32_t result;

	if (value == NULL || address > UINT16_MAX ||
	    (width != 8 && width != 16 && width != 32))
		return AE_BAD_PARAMETER;
	if (width == 8) {
		uint8_t part;
		__asm__ volatile ("inb %w1, %0" : "=a" (part) : "Nd" ((uint16_t)address));
		result = part;
	} else if (width == 16) {
		uint16_t part;
		__asm__ volatile ("inw %w1, %0" : "=a" (part) : "Nd" ((uint16_t)address));
		result = part;
	} else {
		__asm__ volatile ("inl %w1, %0" : "=a" (result) : "Nd" ((uint16_t)address));
	}
	*value = result;
	return AE_OK;
}

ACPI_STATUS
AcpiOsWritePort(ACPI_IO_ADDRESS address, UINT32 value, UINT32 width)
{
	if (address > UINT16_MAX ||
	    (width != 8 && width != 16 && width != 32))
		return AE_BAD_PARAMETER;
	if (width == 8)
		__asm__ volatile ("outb %0, %w1" : : "a" ((uint8_t)value), "Nd" ((uint16_t)address));
	else if (width == 16)
		__asm__ volatile ("outw %0, %w1" : : "a" ((uint16_t)value), "Nd" ((uint16_t)address));
	else
		__asm__ volatile ("outl %0, %w1" : : "a" (value), "Nd" ((uint16_t)address));
	return AE_OK;
}

ACPI_STATUS
AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS address, UINT64 *value, UINT32 width)
{
	volatile uint8_t *pointer;

	if (value == NULL ||
	    (width != 8 && width != 16 && width != 32 && width != 64))
		return AE_BAD_PARAMETER;
	pointer = AcpiOsMapMemory(address, width / 8);
	if (pointer == NULL)
		return AE_BAD_PARAMETER;
	*value = 0;
	for (UINT32 i = 0; i < width / 8; i++)
		*value |= (uint64_t)pointer[i] << (i * 8);
	AcpiOsUnmapMemory((void *)pointer, width / 8);
	return AE_OK;
}

ACPI_STATUS
AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS address, UINT64 value, UINT32 width)
{
	volatile uint8_t *pointer;

	if (width != 8 && width != 16 && width != 32 && width != 64)
		return AE_BAD_PARAMETER;
	pointer = AcpiOsMapMemory(address, width / 8);
	if (pointer == NULL)
		return AE_BAD_PARAMETER;
	for (UINT32 i = 0; i < width / 8; i++)
		pointer[i] = (uint8_t)(value >> (i * 8));
	AcpiOsUnmapMemory((void *)pointer, width / 8);
	return AE_OK;
}

ACPI_STATUS
AcpiOsReadPciConfiguration(ACPI_PCI_ID *id, UINT32 reg, UINT64 *value,
	UINT32 width)
{
	(void)id;
	(void)reg;
	(void)value;
	(void)width;
	return AE_SUPPORT;
}

ACPI_STATUS
AcpiOsWritePciConfiguration(ACPI_PCI_ID *id, UINT32 reg, UINT64 value,
	UINT32 width)
{
	(void)id;
	(void)reg;
	(void)value;
	(void)width;
	return AE_SUPPORT;
}

BOOLEAN
AcpiOsReadable(void *pointer, ACPI_SIZE length)
{
	return pointer != NULL && length != 0;
}

BOOLEAN
AcpiOsWritable(void *pointer, ACPI_SIZE length)
{
	return pointer != NULL && length != 0;
}

UINT64
AcpiOsGetTimer(void)
{
	return timer_ticks() * 100000ULL;
}

ACPI_STATUS
AcpiOsSignal(UINT32 function, void *info)
{
	(void)info;
	if (function == ACPI_SIGNAL_FATAL)
		klog("ACPICA fatal signal\n");
	return AE_SUPPORT;
}

ACPI_STATUS
AcpiOsEnterSleep(UINT8 state, UINT32 reg_a, UINT32 reg_b)
{
	(void)state;
	(void)reg_a;
	(void)reg_b;
	return AE_SUPPORT;
}

void
AcpiOsPrintf(const char *format, ...)
{
	(void)format;
}

void
AcpiOsVprintf(const char *format, va_list args)
{
	(void)format;
	(void)args;
}

void
AcpiOsRedirectOutput(void *destination)
{
	(void)destination;
}

void
AcpiOsTracePoint(ACPI_TRACE_EVENT_TYPE type, BOOLEAN begin, UINT8 *aml,
	char *pathname)
{
	(void)type;
	(void)begin;
	(void)aml;
	(void)pathname;
}
