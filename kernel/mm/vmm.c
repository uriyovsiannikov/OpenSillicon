#include <stddef.h>
#include <stdint.h>

#include "kernel/log.h"
#include "kernel/pmm.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096ULL
#define PTE_PRESENT 0x001ULL
#define PTE_WRITE 0x002ULL
#define PTE_USER 0x004ULL
#define PTE_PWT 0x008ULL
#define PTE_PCD 0x010ULL
#define PTE_LARGE 0x080ULL
#define PTE_NX (1ULL << 63)
#define PTE_ADDRESS 0x000ffffffffff000ULL
#define USER_BASE 0x0000000000010000ULL
#define USER_END 0x00007ffffffff000ULL
#define KERNEL_BASE 0xffff800000000000ULL
#define VMALLOC_BASE 0xffffc00000000000ULL
#define VMALLOC_END 0xffffe00000000000ULL
#define HEAP_BASE 0xffffe00000000000ULL
#define HEAP_END 0xfffff00000000000ULL

extern char __kernel_start[];
extern char __kernel_end[];

static uint64_t hhdm_offset;
static struct address_space kernel_space;
static struct address_space *active_space;
static struct address_space *address_spaces;
static bool initialized;

static bool
address_space_valid(const struct address_space *space)
{
	const struct address_space *current;

	if (space == &kernel_space)
		return true;
	for (current = address_spaces; current != NULL; current = current->next) {
		if (current == space)
			return true;
	}
	return false;
}

static uint64_t
read_cr3(void)
{
	uint64_t value;

	__asm__ volatile ("mov %%cr3, %0" : "=r" (value));
	return value & PTE_ADDRESS;
}

static bool
canonical(uint64_t address)
{
	uint64_t upper = address >> 47;

	return upper == 0 || upper == 0x1ffff;
}

static uint64_t *
table_virtual(uint64_t physical)
{
	if (physical > UINT64_MAX - hhdm_offset)
		return NULL;
	return (uint64_t *)(uintptr_t)(hhdm_offset + physical);
}

static bool
new_table(uint64_t *physical, uint64_t **virtual)
{
	if (!pmm_alloc_page(physical))
		return false;
	*virtual = table_virtual(*physical);
	if (*virtual == NULL) {
		(void)pmm_free_page(*physical);
		return false;
	}
	for (size_t i = 0; i < PAGE_SIZE / sizeof(**virtual); i++)
		(*virtual)[i] = 0;
	return true;
}

static bool
cpu_has_nx(void)
{
	uint32_t eax = 0x80000000U;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;

	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx));
	if (eax < 0x80000001U)
		return false;
	eax = 0x80000001U;
	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx));
	return (edx & (1U << 20)) != 0;
}

static bool
validate_layout(const struct boot_info *info)
{
	uint64_t physical_end = 0;
	uint64_t hhdm_end;
	uint64_t i;
	uint64_t kernel_start = (uintptr_t)__kernel_start;
	uint64_t kernel_end = (uintptr_t)__kernel_end;

	for (i = 0; i < info->memmap->entry_count; i++) {
		const struct limine_memmap_entry *entry = info->memmap->entries[i];
		uint64_t end;

		if (entry->length == 0 || entry->base > UINT64_MAX - entry->length)
			return false;
		end = entry->base + entry->length;
		if (end > physical_end)
			physical_end = end;
	}
	if (info->hhdm_offset > UINT64_MAX - physical_end)
		return false;
	hhdm_end = info->hhdm_offset + physical_end;
	if (!canonical(info->hhdm_offset) || info->hhdm_offset < KERNEL_BASE ||
	    hhdm_end == 0 ||
	    !canonical(hhdm_end - 1))
		return false;
	if (info->hhdm_offset < kernel_end && kernel_start < hhdm_end)
		return false;
	if (info->hhdm_offset < VMALLOC_END && VMALLOC_BASE < hhdm_end)
		return false;
	if (info->hhdm_offset < HEAP_END && HEAP_BASE < hhdm_end)
		return false;
	if (kernel_end <= kernel_start ||
	    (kernel_start < VMALLOC_END && VMALLOC_BASE < kernel_end) ||
	    (kernel_start < HEAP_END && HEAP_BASE < kernel_end))
		return false;
	return true;
}

bool
vmm_init(const struct boot_info *info)
{
	uint32_t efer_low;
	uint32_t efer_high;

	if (initialized || info == NULL || info->memmap == NULL ||
	    info->memmap->entry_count == 0 || info->memmap->entries == NULL ||
	    info->hhdm_offset == 0)
		return false;
	if (!validate_layout(info)) {
		klog("OpenSillicon: VMM virtual ranges overlap\n");
		return false;
	}
	if (!cpu_has_nx()) {
		klog("OpenSillicon: CPU does not support NX\n");
		return false;
	}
	hhdm_offset = info->hhdm_offset;
	__asm__ volatile ("rdmsr" : "=a" (efer_low), "=d" (efer_high) :
	    "c" (0xc0000080U));
	efer_low |= 1U << 11;
	__asm__ volatile ("wrmsr" : : "c" (0xc0000080U), "a" (efer_low),
	    "d" (efer_high));
	__asm__ volatile ("mov %%cr0, %%rax; or $0x10000, %%rax; mov %%rax, %%cr0"
	    : : : "rax", "memory");
	kernel_space.root_physical = read_cr3();
	active_space = &kernel_space;
	initialized = true;
	return true;
}

static bool
map_in_root(uint64_t root_physical, uint64_t virtual_address,
	uint64_t physical_address, unsigned int flags)
{
	static const unsigned int shifts[] = {39, 30, 21};
	uint64_t *allocated_parents[3];
	uint64_t allocated_physical[3];
	unsigned int allocated_indices[3];
	unsigned int allocated_count = 0;
	uint64_t *table = table_virtual(root_physical);
	uint64_t entry_flags = PTE_PRESENT;
	unsigned int i;

	if (table == NULL)
		return false;
	if ((flags & VMM_WRITE) != 0)
		entry_flags |= PTE_WRITE;
	if ((flags & VMM_USER) != 0)
		entry_flags |= PTE_USER;
	if ((flags & VMM_UNCACHED) != 0)
		entry_flags |= PTE_PCD | PTE_PWT;
	if ((flags & VMM_EXEC) == 0)
		entry_flags |= PTE_NX;
	for (i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
		unsigned int index = (unsigned int)((virtual_address >> shifts[i]) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & PTE_PRESENT) == 0) {
			uint64_t child_physical;
			uint64_t *child;

			if (!new_table(&child_physical, &child))
				goto fail;
			allocated_parents[allocated_count] = table;
			allocated_indices[allocated_count] = index;
			allocated_physical[allocated_count] = child_physical;
			allocated_count++;
			table[index] = child_physical | PTE_PRESENT | PTE_WRITE;
			if ((flags & VMM_USER) != 0)
				table[index] |= PTE_USER;
			table = child;
			continue;
		}
		if ((entry & PTE_LARGE) != 0)
			goto fail;
		if ((flags & VMM_WRITE) != 0)
			entry |= PTE_WRITE;
		if ((flags & VMM_USER) != 0)
			entry |= PTE_USER;
		if ((flags & VMM_EXEC) != 0)
			entry &= ~PTE_NX;
		table[index] = entry;
		table = table_virtual(entry & PTE_ADDRESS);
		if (table == NULL)
			goto fail;
	}
	{
		unsigned int index = (unsigned int)((virtual_address >> 12) & 0x1ff);

		if ((table[index] & PTE_PRESENT) != 0)
			goto fail;
		table[index] = (physical_address & PTE_ADDRESS) | entry_flags;
	}
	return true;

fail:
	while (allocated_count != 0) {
		allocated_count--;
		allocated_parents[allocated_count][allocated_indices[allocated_count]] = 0;
		(void)pmm_free_page(allocated_physical[allocated_count]);
	}
	return false;
}

static void
sync_kernel_mapping(uint64_t virtual_address)
{
	uint64_t *kernel_root = table_virtual(kernel_space.root_physical);
	unsigned int index = (unsigned int)((virtual_address >> 39) & 0x1ff);
	struct address_space *space;

	for (space = address_spaces; space != NULL; space = space->next) {
		uint64_t *root = table_virtual(space->root_physical);

		if (root != NULL)
			root[index] = kernel_root[index];
	}
}

bool
vmm_map_page(struct address_space *space, uint64_t virtual_address,
	uint64_t physical_address, unsigned int flags)
{
	if (!initialized || !address_space_valid(space) || (flags & ~0x0fU) != 0 ||
	    (flags & (VMM_WRITE | VMM_EXEC)) == (VMM_WRITE | VMM_EXEC) ||
	    (virtual_address & (PAGE_SIZE - 1)) != 0 ||
	    (physical_address & (PAGE_SIZE - 1)) != 0 ||
	    (physical_address & ~PTE_ADDRESS) != 0 || !canonical(virtual_address))
		return false;
	if ((flags & VMM_USER) != 0 &&
	    (space == &kernel_space || virtual_address < USER_BASE ||
	    virtual_address >= USER_END))
		return false;
	if ((flags & VMM_USER) == 0 &&
	    (virtual_address < KERNEL_BASE ||
	    !((virtual_address >= VMALLOC_BASE && virtual_address < VMALLOC_END) ||
	    (virtual_address >= HEAP_BASE && virtual_address < HEAP_END))))
		return false;
	if (!map_in_root((flags & VMM_USER) != 0 ? space->root_physical :
	    kernel_space.root_physical, virtual_address, physical_address,
	    flags))
		return false;
	if ((flags & VMM_USER) == 0)
		sync_kernel_mapping(virtual_address);
	__asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
	    : "memory");
	return true;
}

bool
vmm_unmap_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address)
{
	static const unsigned int shifts[] = {39, 30, 21};
	uint64_t *table;
	unsigned int i;

	if (!initialized || !address_space_valid(space) || physical_address == NULL ||
	    (virtual_address & (PAGE_SIZE - 1)) != 0 || !canonical(virtual_address) ||
	    virtual_address < USER_BASE ||
	    (virtual_address >= USER_END && virtual_address < KERNEL_BASE) ||
	    (virtual_address >= KERNEL_BASE &&
	    !((virtual_address >= VMALLOC_BASE && virtual_address < VMALLOC_END) ||
	    (virtual_address >= HEAP_BASE && virtual_address < HEAP_END))))
		return false;
	if (virtual_address < KERNEL_BASE && space == &kernel_space)
		return false;
	table = table_virtual(virtual_address < KERNEL_BASE ? space->root_physical :
	    kernel_space.root_physical);
	if (table == NULL)
		return false;
	for (i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
		unsigned int index = (unsigned int)((virtual_address >> shifts[i]) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0)
			return false;
		table = table_virtual(entry & PTE_ADDRESS);
		if (table == NULL)
			return false;
	}
	{
		unsigned int index = (unsigned int)((virtual_address >> 12) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & PTE_PRESENT) == 0)
			return false;
		*physical_address = entry & PTE_ADDRESS;
		table[index] = 0;
	}
	__asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
	    : "memory");
	return true;
}

bool
address_space_create(struct address_space *space)
{
	uint64_t *root;
	uint64_t physical;
	uint64_t *kernel_root;
	unsigned int i;

	if (!initialized || space == NULL || address_space_valid(space) ||
	    !new_table(&physical, &root))
		return false;
	kernel_root = table_virtual(kernel_space.root_physical);
	if (kernel_root == NULL) {
		(void)pmm_free_page(physical);
		return false;
	}
	for (i = 256; i < 512; i++)
		root[i] = kernel_root[i];
	space->root_physical = physical;
	space->next = address_spaces;
	address_spaces = space;
	return true;
}

bool
address_space_activate(const struct address_space *space)
{
	if (!initialized || !address_space_valid(space) || space->root_physical == 0 ||
	    (space->root_physical & (PAGE_SIZE - 1)) != 0 ||
	    table_virtual(space->root_physical) == NULL)
		return false;
	__asm__ volatile ("mov %0, %%cr3" : : "r" (space->root_physical) : "memory");
	active_space = (struct address_space *)space;
	return true;
}

static bool
validate_user_tables(uint64_t table_physical, unsigned int level)
{
	uint64_t *table = table_virtual(table_physical);
	unsigned int i;

	if (table == NULL)
		return false;
	for (i = 0; i < 512; i++) {
		uint64_t entry = table[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if (level > 1) {
			if ((entry & PTE_LARGE) != 0 ||
			    !validate_user_tables(entry & PTE_ADDRESS, level - 1))
				return false;
		}
	}
	return true;
}

static void
free_user_tables(uint64_t table_physical, unsigned int level)
{
	uint64_t *table = table_virtual(table_physical);
	unsigned int i;

	if (level > 1) {
		for (i = 0; i < 512; i++) {
			uint64_t entry = table[i];

			if ((entry & PTE_PRESENT) != 0)
				free_user_tables(entry & PTE_ADDRESS, level - 1);
		}
	}
	if (level > 1) {
		for (i = 0; i < 512; i++) {
			uint64_t entry = table[i];

			if ((entry & PTE_PRESENT) != 0)
				(void)pmm_free_page(entry & PTE_ADDRESS);
		}
	}
}

bool
address_space_destroy(struct address_space *space)
{
	uint64_t *root;
	unsigned int i;

	if (!initialized || !address_space_valid(space) || space->root_physical == 0 ||
	    space->root_physical == kernel_space.root_physical ||
	    space->root_physical == read_cr3())
		return false;
	root = table_virtual(space->root_physical);
	if (root == NULL)
		return false;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if ((entry & PTE_LARGE) != 0 ||
		    !validate_user_tables(entry & PTE_ADDRESS, 3))
			return false;
	}
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0)
			free_user_tables(entry & PTE_ADDRESS, 3);
	}
	(void)pmm_free_page(space->root_physical);
	{
		struct address_space **link = &address_spaces;

		while (*link != NULL && *link != space)
			link = &(*link)->next;
		if (*link == space)
			*link = space->next;
	}
	space->root_physical = 0;
	space->next = NULL;
	return true;
}

static bool
user_page_physical(const struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address, bool writable, bool executable)
{
	static const unsigned int shifts[] = {39, 30, 21};
	uint64_t *table = table_virtual(space->root_physical);
	unsigned int i;

	if (table == NULL)
		return false;
	for (i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
		unsigned int index = (unsigned int)((virtual_address >> shifts[i]) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
		    (writable && (entry & PTE_WRITE) == 0) ||
		    (executable && (entry & PTE_NX) != 0) ||
		    (entry & PTE_LARGE) != 0)
			return false;
		table = table_virtual(entry & PTE_ADDRESS);
		if (table == NULL)
			return false;
	}
	{
		unsigned int index = (unsigned int)((virtual_address >> 12) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER) ||
		    (writable && (entry & PTE_WRITE) == 0) ||
		    (executable && (entry & PTE_NX) != 0))
			return false;
		*physical_address = entry & PTE_ADDRESS;
	}
	return true;
}

bool
vmm_copy_from_user(const struct address_space *space, void *destination,
	uint64_t source, size_t length)
{
	uint64_t current;
	uint64_t remaining;
	uintptr_t output = (uintptr_t)destination;

	if (!initialized || !address_space_valid(space) || space == &kernel_space ||
	    (length != 0 && destination == NULL))
		return false;
	if (length == 0)
		return true;
	if (source < USER_BASE || source >= USER_END || length > USER_END - source ||
	    length - 1 > UINTPTR_MAX - output)
		return false;
	current = source;
	remaining = length;
	while (remaining != 0) {
		uint64_t physical;
		uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, false, false))
			return false;
		current += chunk;
		remaining -= chunk;
	}
	current = source;
	remaining = length;
	while (remaining != 0) {
		uint64_t physical;
		uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));
		const volatile uint8_t *input;
		uint64_t i;

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, false, false) ||
		    hhdm_offset > UINT64_MAX - physical ||
		    chunk - 1 > UINTPTR_MAX - (uintptr_t)(hhdm_offset + physical +
		    (current & (PAGE_SIZE - 1))))
			return false;
		input = (const volatile uint8_t *)(uintptr_t)(hhdm_offset + physical +
		    (current & (PAGE_SIZE - 1)));
		for (i = 0; i < chunk; i++)
			((uint8_t *)output)[i] = input[i];
		output += chunk;
		current += chunk;
		remaining -= chunk;
	}
	return true;
}

bool
vmm_copy_to_user(const struct address_space *space, uint64_t destination,
	const void *source, size_t length)
{
	uint64_t current;
	uint64_t remaining;
	uintptr_t input = (uintptr_t)source;

	if (!initialized || !address_space_valid(space) || space == &kernel_space ||
	    (length != 0 && source == NULL))
		return false;
	if (length == 0)
		return true;
	if (destination < USER_BASE || destination >= USER_END ||
	    length > USER_END - destination || length - 1 > UINTPTR_MAX - input)
		return false;
	current = destination;
	remaining = length;
	while (remaining != 0) {
		uint64_t physical;
		uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, true, false))
			return false;
		current += chunk;
		remaining -= chunk;
	}
	current = destination;
	remaining = length;
	while (remaining != 0) {
		uint64_t physical;
		uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));
		volatile uint8_t *output;
		uint64_t i;

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, true, false) ||
		    hhdm_offset > UINT64_MAX - physical ||
		    (current & (PAGE_SIZE - 1)) > UINTPTR_MAX -
		    (uintptr_t)(hhdm_offset + physical) ||
		    chunk - 1 > UINTPTR_MAX - (uintptr_t)(hhdm_offset + physical +
		    (current & (PAGE_SIZE - 1))))
			return false;
		output = (volatile uint8_t *)(uintptr_t)(hhdm_offset + physical +
		    (current & (PAGE_SIZE - 1)));
		for (i = 0; i < chunk; i++)
			output[i] = ((const uint8_t *)input)[i];
		input += chunk;
		current += chunk;
		remaining -= chunk;
	}
	return true;
}

bool
vmm_validate_user_write(const struct address_space *space,
	uint64_t destination, size_t length)
{
	uint64_t current;
	uint64_t remaining;

	if (!initialized || !address_space_valid(space) || space == &kernel_space)
		return false;
	if (length == 0)
		return true;
	if (destination < USER_BASE || destination >= USER_END ||
	    length > USER_END - destination)
		return false;
	current = destination;
	remaining = length;
	while (remaining != 0) {
		uint64_t physical;
		uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, true, false))
			return false;
		current += chunk;
		remaining -= chunk;
	}
	return true;
}

bool
vmm_validate_user_exec(const struct address_space *space,
	uint64_t address, size_t length)
{
	uint64_t current;
	uint64_t remaining;

	if (!initialized || !address_space_valid(space) || space == &kernel_space)
		return false;
	if (length == 0)
		return true;
	if (address < USER_BASE || address >= USER_END ||
	    length > USER_END - address)
		return false;
	current = address;
	remaining = length;
	while (remaining != 0) {
		uint64_t physical;
		uint64_t chunk = PAGE_SIZE - (current & (PAGE_SIZE - 1));

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, false, true))
			return false;
		current += chunk;
		remaining -= chunk;
	}
	return true;
}

static bool
copy_physical_page(uint64_t source, uint64_t destination)
{
	volatile uint8_t *input;
	volatile uint8_t *output;
	uint64_t i;

	if (hhdm_offset > UINT64_MAX - source ||
	    hhdm_offset > UINT64_MAX - destination ||
	    PAGE_SIZE - 1 > UINT64_MAX - (hhdm_offset + source) ||
	    PAGE_SIZE - 1 > UINT64_MAX - (hhdm_offset + destination))
		return false;
	input = (volatile uint8_t *)(uintptr_t)(hhdm_offset + source);
	output = (volatile uint8_t *)(uintptr_t)(hhdm_offset + destination);
	for (i = 0; i < PAGE_SIZE; i++)
		output[i] = input[i];
	return true;
}

static bool
clone_user_tables(uint64_t table_physical, unsigned int level,
	uint64_t virtual_prefix, struct address_space *destination)
{
	uint64_t *table = table_virtual(table_physical);
	unsigned int i;

	if (table == NULL)
		return false;
	for (i = 0; i < 512; i++) {
		uint64_t entry = table[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if ((entry & PTE_USER) == 0 || (entry & PTE_LARGE) != 0)
			return false;
		if (level > 1) {
			if (!clone_user_tables(entry & PTE_ADDRESS, level - 1,
			    virtual_prefix | ((uint64_t)i << (level == 3 ? 30 : 21)),
			    destination))
				return false;
		} else {
			uint64_t virtual_address = virtual_prefix |
			    ((uint64_t)i << 12);
			uint64_t copy_physical;
			unsigned int flags = VMM_USER;

			if ((entry & PTE_WRITE) != 0)
				flags |= VMM_WRITE;
			if ((entry & PTE_NX) == 0)
				flags |= VMM_EXEC;
			if ((entry & PTE_PCD) != 0)
				flags |= VMM_UNCACHED;
			if (!pmm_alloc_page(&copy_physical))
				return false;
			if (!copy_physical_page(entry & PTE_ADDRESS, copy_physical)) {
				(void)pmm_free_page(copy_physical);
				return false;
			}
			if (!vmm_map_page(destination, virtual_address, copy_physical,
			    flags)) {
				(void)pmm_free_page(copy_physical);
				return false;
			}
		}
	}
	return true;
}

static bool
free_user_leaves(uint64_t table_physical, unsigned int level)
{
	uint64_t *table = table_virtual(table_physical);
	unsigned int i;

	if (table == NULL)
		return false;
	for (i = 0; i < 512; i++) {
		uint64_t entry = table[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if (level > 1) {
			if ((entry & PTE_LARGE) != 0 ||
			    !free_user_leaves(entry & PTE_ADDRESS, level - 1))
				return false;
		} else {
			if (!pmm_free_page(entry & PTE_ADDRESS))
				return false;
			table[i] = 0;
		}
	}
	return true;
}

bool
address_space_destroy_with_pages(struct address_space *space)
{
	uint64_t *root;
	unsigned int i;

	if (!initialized || !address_space_valid(space) || space == &kernel_space ||
	    space->root_physical == 0 || space->root_physical == read_cr3())
		return false;
	root = table_virtual(space->root_physical);
	if (root == NULL)
		return false;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    ((entry & PTE_LARGE) != 0 ||
		    !validate_user_tables(entry & PTE_ADDRESS, 3)))
			return false;
	}
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    !free_user_leaves(entry & PTE_ADDRESS, 3))
			return false;
	}
	return address_space_destroy(space);
}

bool
address_space_clone(const struct address_space *source,
	struct address_space *destination)
{
	uint64_t *root;
	unsigned int i;

	if (!initialized || source == NULL || source == &kernel_space ||
	    !address_space_valid(source) || destination == NULL ||
	    !address_space_create(destination))
		return false;
	root = table_virtual(source->root_physical);
	if (root == NULL)
		goto fail;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if ((entry & PTE_USER) == 0 || (entry & PTE_LARGE) != 0 ||
		    !clone_user_tables(entry & PTE_ADDRESS, 3,
		    (uint64_t)i << 39, destination))
			goto fail;
	}
	return true;

fail:
	if (!address_space_destroy_with_pages(destination))
		return false;
	return false;
}

uint64_t
vmm_hhdm_offset(void)
{
	return initialized ? hhdm_offset : 0;
}

struct address_space *
vmm_active_address_space(void)
{
	return initialized ? active_space : NULL;
}

struct address_space *
vmm_kernel_address_space(void)
{
	return initialized ? &kernel_space : NULL;
}

uint64_t
vmm_heap_start(void)
{
	return HEAP_BASE;
}

uint64_t
vmm_heap_end(void)
{
	return HEAP_END;
}

uint64_t
vmm_vmalloc_start(void)
{
	return VMALLOC_BASE;
}

uint64_t
vmm_vmalloc_end(void)
{
	return VMALLOC_END;
}
