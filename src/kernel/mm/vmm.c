#include <stddef.h>
#include <stdint.h>
#include "arch/x86_64/copy.h"

#include "kernel/log.h"
#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/pmm.h"
#include "kernel/smp.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096ULL
#define PTE_PRESENT 0x001ULL
#define PTE_WRITE 0x002ULL
#define PTE_USER 0x004ULL
#define PTE_PWT 0x008ULL
#define PTE_PCD 0x010ULL
#define PTE_LARGE 0x080ULL
#define PTE_SHARED 0x200ULL
#define PTE_PRIVATE 0x400ULL
#define PTE_NX (1ULL << 63)
#define PTE_ADDRESS 0x000ffffffffff000ULL
#define VMM_PRIVATE 0x10U
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
static const struct limine_memmap_response *physical_memory_map;
static struct address_space kernel_space;
static struct address_space *fallback_active_space;
static struct address_space *address_spaces;
static bool initialized;
static struct spinlock address_space_lifecycle_lock = SPINLOCK_INITIALIZER;
static struct spinlock kernel_mapping_lock = SPINLOCK_INITIALIZER;
static bool unmap_user_page(struct address_space *space,
	uint64_t virtual_address, uint64_t *physical_address, bool allow_private);
#ifdef KERNEL_TEST_SHM_FORK_FAILURE
static bool test_shm_fork_failure_armed;
static size_t test_shm_fork_mapped_pages;
#endif
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
static unsigned int test_private_map_fail_after;
static unsigned int test_private_fork_fail_after;
static unsigned int test_private_map_page_count;
static unsigned int test_private_fork_page_count;
static unsigned int test_private_metadata_fail_at;
static unsigned int test_private_metadata_allocations;
static uint64_t test_private_collision_physical;
#endif

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

/* Address-space mutation stays BSP-owned until remote TLB shootdowns and
 * page-table publication are synchronized for every AP. */
static bool
vmm_mutation_allowed(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu == NULL || cpu->bsp;
}

static bool
vmm_ap_heap_mapping_allowed(const struct address_space *space,
	uint64_t virtual_address)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu != NULL && !cpu->bsp && space == &kernel_space &&
	    virtual_address >= HEAP_BASE && virtual_address < HEAP_END;
}

static struct address_space **
active_space_slot(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu == NULL ? &fallback_active_space : &cpu->active_address_space;
}

static struct address_space *
current_active_space(void)
{
	return __atomic_load_n(active_space_slot(), __ATOMIC_ACQUIRE);
}

static bool
address_space_active_on_any_cpu(const struct address_space *space)
{
	size_t count = smp_discovered_count();

	if (count == 0)
		return __atomic_load_n(&fallback_active_space, __ATOMIC_ACQUIRE) == space;
	for (size_t i = 0; i < count; i++) {
		const struct smp_cpu *cpu = smp_cpu_at(i);

		if (cpu != NULL && __atomic_load_n(&cpu->active_address_space,
		    __ATOMIC_ACQUIRE) == space)
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

static void
shootdown_tlbs(const struct address_space *space)
{
	/* Kernel mappings are published under kernel_mapping_lock. User address
	 * spaces remain BSP-only until their full lifecycle is SMP-safe. */
	if (!(space == NULL || space == &kernel_space ? smp_tlb_shootdown_all() :
	    smp_tlb_shootdown_space(space))) {
		klog("OpenSillicon panic: remote TLB shootdown failed\n");
		kernel_halt();
	}
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
	if (info->hhdm_offset < VMM_MODULE_ARENA_END &&
	    VMM_MODULE_ARENA_START < hhdm_end)
		return false;
	if (kernel_end <= kernel_start ||
	    (kernel_start < VMALLOC_END && VMALLOC_BASE < kernel_end) ||
	    (kernel_start < HEAP_END && HEAP_BASE < kernel_end) ||
	    (kernel_start < VMM_MODULE_ARENA_END &&
	    VMM_MODULE_ARENA_START < kernel_end))
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
	physical_memory_map = info->memmap;
	__asm__ volatile ("rdmsr" : "=a" (efer_low), "=d" (efer_high) :
	    "c" (0xc0000080U));
	efer_low |= 1U << 11;
	__asm__ volatile ("wrmsr" : : "c" (0xc0000080U), "a" (efer_low),
	    "d" (efer_high));
	__asm__ volatile ("mov %%cr0, %%rax; or $0x10000, %%rax; mov %%rax, %%cr0"
	    : : : "rax", "memory");
	kernel_space.root_physical = read_cr3();
	__atomic_store_n(&fallback_active_space, &kernel_space, __ATOMIC_RELEASE);
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
	if ((flags & VMM_PRIVATE) != 0)
		entry_flags |= PTE_PRIVATE;
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
	bool kernel_mapping = (flags & VMM_USER) == 0;
	uint64_t kernel_lock_flags = 0;

	if ((!vmm_mutation_allowed() &&
	    !vmm_ap_heap_mapping_allowed(space, virtual_address)) || !initialized ||
	    !address_space_valid(space) || (flags & ~0x1fU) != 0 ||
	    (flags & (VMM_WRITE | VMM_EXEC)) == (VMM_WRITE | VMM_EXEC) ||
	    (virtual_address & (PAGE_SIZE - 1)) != 0 ||
	    (physical_address & (PAGE_SIZE - 1)) != 0 ||
	    (physical_address & ~PTE_ADDRESS) != 0 || !canonical(virtual_address))
		return false;
	if ((flags & VMM_USER) != 0 &&
	    (space == &kernel_space || virtual_address < USER_BASE ||
	    virtual_address >= USER_END))
		return false;
	if ((flags & VMM_PRIVATE) != 0 &&
	    ((flags & (VMM_USER | VMM_EXEC)) != VMM_USER ||
	    virtual_address < PRIVATE_MAP_USABLE_START ||
	    virtual_address >= PRIVATE_MAP_USABLE_END))
		return false;
	if ((flags & VMM_USER) != 0 && (flags & VMM_PRIVATE) == 0 &&
	    virtual_address >= PRIVATE_MAP_ARENA_START &&
	    virtual_address < PRIVATE_MAP_ARENA_END)
		return false;
	if ((flags & VMM_USER) == 0 &&
	    (virtual_address < KERNEL_BASE ||
	    !((virtual_address >= VMALLOC_BASE && virtual_address < VMALLOC_END) ||
	    (virtual_address >= HEAP_BASE && virtual_address < HEAP_END) ||
	    (virtual_address >= VMM_MODULE_ARENA_START &&
	    virtual_address < VMM_MODULE_ARENA_END))))
		return false;
	if (kernel_mapping)
		kernel_lock_flags = spinlock_lock_irqsave(&kernel_mapping_lock);
	if (!map_in_root(kernel_mapping ? kernel_space.root_physical :
	    space->root_physical, virtual_address, physical_address,
	    flags)) {
		if (kernel_mapping)
			spinlock_unlock_irqrestore(&kernel_mapping_lock,
			    kernel_lock_flags);
		return false;
	}
	if (kernel_mapping)
		sync_kernel_mapping(virtual_address);
	__asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
	    : "memory");
	shootdown_tlbs(kernel_mapping ? NULL : space);
	if (kernel_mapping)
		spinlock_unlock_irqrestore(&kernel_mapping_lock, kernel_lock_flags);
	return true;
}

static bool
user_leaf_entry(const struct address_space *space, uint64_t virtual_address,
	uint64_t **leaf_table, unsigned int *leaf_index)
{
	static const unsigned int shifts[] = {39, 30, 21};
	uint64_t *table;
	unsigned int i;

	if (space == NULL || (virtual_address & (PAGE_SIZE - 1)) != 0 ||
	    virtual_address < USER_BASE || virtual_address >= USER_END)
		return false;
	table = table_virtual(space->root_physical);
	if (table == NULL)
		return false;
	for (i = 0; i < sizeof(shifts) / sizeof(shifts[0]); i++) {
		unsigned int index = (unsigned int)((virtual_address >> shifts[i]) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & (PTE_PRESENT | PTE_USER)) !=
		    (PTE_PRESENT | PTE_USER) || (entry & PTE_LARGE) != 0 ||
		    (entry & (PTE_SHARED | PTE_PRIVATE)) != 0)
			return false;
		table = table_virtual(entry & PTE_ADDRESS);
		if (table == NULL)
			return false;
	}
	*leaf_table = table;
	*leaf_index = (unsigned int)((virtual_address >> 12) & 0x1ff);
	return true;
}

static bool
map_shared_page(struct address_space *space, uint64_t virtual_address,
	uint64_t physical_address)
{
	uint64_t *leaf_table;
	unsigned int leaf_index;
	uint64_t mapped_physical;

	if (!vmm_map_page(space, virtual_address, physical_address,
	    VMM_USER | VMM_WRITE))
		return false;
	if (!user_leaf_entry(space, virtual_address, &leaf_table, &leaf_index)) {
		if (!vmm_unmap_page(space, virtual_address, &mapped_physical) ||
		    mapped_physical != physical_address) {
			klog("OpenSillicon panic: shared page marking rollback failed\n");
			kernel_halt();
		}
		return false;
	}
	leaf_table[leaf_index] |= PTE_SHARED;
	/* vmm_map_page has already invalidated this translation; invalidate again
	 * after publishing the software ownership marker. */
	__asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
	    : "memory");
	return true;
}

static bool
unmap_shared_page(void *context, uint64_t virtual_address,
	uint64_t *physical_address)
{
	struct address_space *space = context;
	uint64_t *leaf_table;
	unsigned int leaf_index;
	uint64_t entry;

	if (physical_address == NULL ||
	    !user_leaf_entry(space, virtual_address, &leaf_table, &leaf_index))
		return false;
	entry = leaf_table[leaf_index];
	if ((entry & (PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_SHARED | PTE_NX)) !=
	    (PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_SHARED | PTE_NX))
		return false;
	*physical_address = entry & PTE_ADDRESS;
	leaf_table[leaf_index] = 0;
	__asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
	    : "memory");
	shootdown_tlbs(space);
	return true;
}

bool
vmm_map_shared_object(struct address_space *space, uint64_t base,
	struct shm_object *object)
{
	uint64_t bytes;
	size_t installed = 0;
	size_t i;

	if (!vmm_mutation_allowed() || !initialized ||
	    !address_space_valid(space) || space == &kernel_space ||
	    object == NULL || object->references == 0 || object->page_count == 0 ||
	    object->page_count > UINT64_MAX / PAGE_SIZE ||
	    (base & (PAGE_SIZE - 1)) != 0)
		return false;
	bytes = (uint64_t)object->page_count * PAGE_SIZE;
	if (base < USER_BASE || base >= USER_END || bytes > USER_END - base)
		return false;
	for (i = 0; i < object->page_count; i++) {
		if (!map_shared_page(space, base + (uint64_t)i * PAGE_SIZE,
		    object->pages[i]))
			break;
		installed++;
	}
	if (installed == object->page_count &&
	    shm_mapping_attach_owned(&space->shared_mappings, base, object))
		return true;
	while (installed != 0) {
		uint64_t physical;

		installed--;
		if (!unmap_shared_page(space, base +
		    (uint64_t)installed * PAGE_SIZE, &physical) ||
		    physical != object->pages[installed]) {
			klog("OpenSillicon panic: shared mapping install rollback failed\n");
			kernel_halt();
		}
	}
	return false;
}

bool
vmm_unmap_shared_object(struct address_space *space, uint64_t base)
{
	if (!vmm_mutation_allowed() || !initialized ||
	    !address_space_valid(space) || space == &kernel_space)
		return false;
	return shm_mapping_detach(&space->shared_mappings, base,
	    unmap_shared_page, space);
}

int
vmm_shm_create(struct address_space *space, uint64_t size,
	uint64_t *base_out)
{
	const uint64_t arena_limit = SHM_ARENA_END - PAGE_SIZE;
	struct shm_object *object;
	uint64_t bytes;
	uint64_t base;
	uint64_t available;
	uint64_t next;
	size_t page_count;

	if (!vmm_mutation_allowed() || base_out == NULL || !initialized ||
	    !address_space_valid(space) ||
	    space == &kernel_space || size == 0 ||
	    size > UINT64_MAX - (PAGE_SIZE - 1))
		return -EINVAL;
	*base_out = 0;
	bytes = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	base = space->shm_next_address;
	if (base < SHM_ARENA_START + PAGE_SIZE || base >= arena_limit ||
	    (base & (PAGE_SIZE - 1)) != 0)
		return -ENOMEM;
	available = arena_limit - base;
	if (bytes > available || PAGE_SIZE > available - bytes)
		return -ENOMEM;
	page_count = (size_t)(bytes / PAGE_SIZE);
	if ((uint64_t)page_count != bytes / PAGE_SIZE ||
	    !shm_object_create(page_count, &object))
		return -ENOMEM;
	if (!vmm_map_shared_object(space, base, object)) {
		if (!shm_object_release(object)) {
			klog("OpenSillicon panic: failed to release uncommitted shared object\n");
			kernel_halt();
		}
		return -ENOMEM;
	}
	next = base + bytes + PAGE_SIZE;
	space->shm_next_address = next;
	*base_out = base;
	return 0;
}

int
vmm_shm_detach(struct address_space *space, uint64_t base)
{
	if (!vmm_mutation_allowed() || !initialized ||
	    !address_space_valid(space) || space == &kernel_space ||
	    base < SHM_ARENA_START + PAGE_SIZE ||
	    base >= SHM_ARENA_END - PAGE_SIZE ||
	    (base & (PAGE_SIZE - 1)) != 0 ||
	    !vmm_unmap_shared_object(space, base))
		return -EINVAL;
	return 0;
}

static bool
unmap_user_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address, bool allow_private)
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
	    (virtual_address >= HEAP_BASE && virtual_address < HEAP_END) ||
	    (virtual_address >= VMM_MODULE_ARENA_START &&
	    virtual_address < VMM_MODULE_ARENA_END))))
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

		if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0 ||
		    (entry & (PTE_SHARED | PTE_PRIVATE)) != 0)
			return false;
		table = table_virtual(entry & PTE_ADDRESS);
		if (table == NULL)
			return false;
	}
	{
		unsigned int index = (unsigned int)((virtual_address >> 12) & 0x1ff);
		uint64_t entry = table[index];

		if ((entry & PTE_PRESENT) == 0 || (entry & PTE_SHARED) != 0 ||
		    (!allow_private && (entry & PTE_PRIVATE) != 0) ||
		    (allow_private && (entry & PTE_PRIVATE) == 0))
			return false;
		*physical_address = entry & PTE_ADDRESS;
		table[index] = 0;
	}
	__asm__ volatile ("invlpg (%0)" : : "r" ((void *)(uintptr_t)virtual_address)
	    : "memory");
	shootdown_tlbs(virtual_address < KERNEL_BASE ? space : NULL);
	return true;
}

static bool
reclaim_empty_user_tables(struct address_space *space, uint64_t virtual_address)
{
	static const unsigned int shifts[] = {39, 30, 21};
	uint64_t *parents[3];
	uint64_t *children[3];
	unsigned int indices[3];
	uint64_t *table = table_virtual(space->root_physical);
	unsigned int i;

	if (table == NULL)
		return false;
	for (i = 0; i < 3; i++) {
		uint64_t entry;

		indices[i] = (unsigned int)((virtual_address >> shifts[i]) & 0x1ff);
		entry = table[indices[i]];
		if ((entry & PTE_PRESENT) == 0 || (entry & PTE_LARGE) != 0)
			return false;
		parents[i] = table;
		table = table_virtual(entry & PTE_ADDRESS);
		if (table == NULL)
			return false;
		children[i] = table;
	}
	for (i = 3; i != 0; i--) {
		uint64_t physical;
		unsigned int j;

		for (j = 0; j < 512; j++) {
			if ((children[i - 1][j] & PTE_PRESENT) != 0)
				break;
		}
		if (j != 512)
			break;
		physical = parents[i - 1][indices[i - 1]] & PTE_ADDRESS;
		parents[i - 1][indices[i - 1]] = 0;
		__asm__ volatile ("" : : : "memory");
		if (!pmm_free_page(physical))
			return false;
	}
	return true;
}

bool
vmm_unmap_page(struct address_space *space, uint64_t virtual_address,
	uint64_t *physical_address)
{
	bool kernel_mapping = virtual_address >= KERNEL_BASE;
	uint64_t kernel_lock_flags = 0;
	bool result;

	if (!vmm_mutation_allowed() &&
	    !vmm_ap_heap_mapping_allowed(space, virtual_address))
		return false;
	if (kernel_mapping)
		kernel_lock_flags = spinlock_lock_irqsave(&kernel_mapping_lock);
	result = unmap_user_page(space, virtual_address, physical_address, false);
	if (kernel_mapping)
		spinlock_unlock_irqrestore(&kernel_mapping_lock, kernel_lock_flags);
	return result;
}

static struct private_mapping *
private_mapping_find(struct address_space *space, uint64_t base,
	struct private_mapping ***link_out)
{
	struct private_mapping **link = &space->private_mappings;

	while (*link != NULL && (*link)->base != base)
		link = &(*link)->next;
	if (link_out != NULL)
		*link_out = link;
	return *link;
}

static void
private_mapping_metadata_destroy(struct private_mapping *mapping)
{
	while (mapping != NULL) {
		struct private_mapping *next = mapping->next;

		if (!kheap_free(mapping->page_protections) ||
		    !kheap_free(mapping->physical_pages) || !kheap_free(mapping)) {
			klog("OpenSillicon panic: failed to release private mapping metadata\n");
			kernel_halt();
		}
		mapping = next;
	}
}

#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
static bool
private_metadata_allocation_fails(void)
{
	test_private_metadata_allocations++;
	if (test_private_metadata_fail_at == 0 ||
	    test_private_metadata_allocations != test_private_metadata_fail_at)
		return false;
	test_private_metadata_fail_at = 0;
	klog("OpenSillicon test: injected private mapping metadata failure\n");
	return true;
}
#endif

static bool
zero_physical_page(uint64_t physical)
{
	volatile uint8_t *page;
	uint64_t offset = vmm_hhdm_offset();
	uint64_t i;

	if (offset > UINT64_MAX - physical ||
	    PAGE_SIZE - 1 > UINT64_MAX - (offset + physical))
		return false;
	page = (volatile uint8_t *)(uintptr_t)(offset + physical);
	for (i = 0; i < PAGE_SIZE; i++)
		page[i] = 0;
	return true;
}

static bool
private_mapping_append(struct address_space *space,
	struct private_mapping *mapping)
{
	struct private_mapping **link = &space->private_mappings;

	while (*link != NULL) {
		uint64_t previous_end;

		if ((*link)->mapped_size > UINT64_MAX - (*link)->base)
			return false;
		previous_end = (*link)->base + (*link)->mapped_size;
		if ((*link)->base >= mapping->base || previous_end > mapping->base)
			return false;
		link = &(*link)->next;
	}
	*link = mapping;
	return true;
}

int
vmm_private_map(struct address_space *space, uint64_t size,
	uint64_t *base_out)
{
	struct private_mapping *mapping = NULL;
	uint64_t rounded_size;
	uint64_t base;
	size_t page_count;
	size_t mapped = 0;
	size_t i;
	int error = -ENOMEM;

	if (!vmm_mutation_allowed() || base_out == NULL || !initialized ||
	    !address_space_valid(space) ||
	    space == &kernel_space || size == 0 ||
	    size > UINT64_MAX - (PAGE_SIZE - 1))
		return -EINVAL;
	*base_out = 0;
	rounded_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	base = space->private_map_cursor;
	if (base < PRIVATE_MAP_USABLE_START || base >= PRIVATE_MAP_USABLE_END ||
	    (base & (PAGE_SIZE - 1)) != 0 ||
	    rounded_size > PRIVATE_MAP_USABLE_END - base)
		return -ENOMEM;
	page_count = (size_t)(rounded_size / PAGE_SIZE);
	if (page_count == 0 || (uint64_t)page_count != rounded_size / PAGE_SIZE ||
	    page_count > SIZE_MAX / sizeof(uint64_t))
		return -EINVAL;
	for (struct private_mapping *other = space->private_mappings;
	    other != NULL; other = other->next) {
		if (base < other->base + other->mapped_size &&
		    other->base < base + rounded_size)
			return -EEXIST;
	}
	if (
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
	    private_metadata_allocation_fails() ||
#endif
	    (mapping = kheap_alloc(sizeof(*mapping))) == NULL)
		return -ENOMEM;
	if (
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
	    private_metadata_allocation_fails() ||
#endif
	    (mapping->physical_pages = kheap_alloc(page_count *
	    sizeof(uint64_t))) == NULL) {
		(void)kheap_free(mapping);
		return -ENOMEM;
	}
	if (
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
	    private_metadata_allocation_fails() ||
#endif
	    (mapping->page_protections = kheap_alloc(page_count)) == NULL) {
		(void)kheap_free(mapping->physical_pages);
		(void)kheap_free(mapping);
		return -ENOMEM;
	}
	mapping->base = base;
	mapping->requested_size = size;
	mapping->mapped_size = rounded_size;
	mapping->page_count = page_count;
	mapping->next = NULL;
	for (i = 0; i < page_count; i++)
		mapping->page_protections[i] = VMM_PROT_READ | VMM_PROT_WRITE;
	for (i = 0; i < page_count; i++) {
		uint64_t physical;
		uint64_t virtual_address = base + (uint64_t)i * PAGE_SIZE;
		uint64_t *leaf_table;
		unsigned int leaf_index;

#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
		if (test_private_map_fail_after != 0 &&
		    test_private_map_page_count >= test_private_map_fail_after) {
			test_private_map_fail_after = 0;
			klog("OpenSillicon test: injected private mapping failure\n");
			goto rollback;
		}
#endif
		if (!pmm_alloc_page(&physical))
			goto rollback;
		if (!zero_physical_page(physical)) {
			if (!pmm_free_page(physical))
				goto invariant_failure;
			goto rollback;
		}
		if (!vmm_map_page(space, virtual_address, physical,
		    VMM_USER | VMM_WRITE | VMM_PRIVATE)) {
			if (!pmm_free_page(physical))
				goto invariant_failure;
			goto rollback;
		}
		if (!user_leaf_entry(space, virtual_address, &leaf_table,
		    &leaf_index) ||
		    (leaf_table[leaf_index] & (PTE_PRESENT | PTE_USER | PTE_WRITE |
		    PTE_PRIVATE | PTE_NX)) != (PTE_PRESENT | PTE_USER | PTE_WRITE |
		    PTE_PRIVATE | PTE_NX)) {
			uint64_t unmapped_physical;

			if (!unmap_user_page(space, virtual_address, &unmapped_physical,
			    true) || unmapped_physical != physical ||
			    !pmm_free_page(physical) ||
			    !reclaim_empty_user_tables(space, virtual_address))
				goto invariant_failure;
			goto rollback;
		}
		mapping->physical_pages[i] = physical;
		mapped++;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
		test_private_map_page_count++;
#endif
	}
	if (!private_mapping_append(space, mapping))
		goto rollback;
	space->private_map_cursor = base + rounded_size;
	*base_out = base;
	return 0;

rollback:
	while (mapped != 0) {
		uint64_t physical;
		uint64_t address;

		mapped--;
		address = base + (uint64_t)mapped * PAGE_SIZE;
		if (!unmap_user_page(space, address,
		    &physical, true) || physical != mapping->physical_pages[mapped] ||
		    !pmm_free_page(physical) ||
		    !reclaim_empty_user_tables(space, address))
			goto invariant_failure;
	}
	private_mapping_metadata_destroy(mapping);
	return error;

invariant_failure:
	klog("OpenSillicon panic: private mapping rollback invariant failed\n");
	kernel_halt();
}

int
vmm_private_map_protected(struct address_space *space, uint64_t size,
	unsigned int protection, uint64_t *base_out)
{
	uint64_t base;
	int result;

	if (base_out == NULL)
		return -EINVAL;
	if (!vmm_private_protection_valid(protection)) {
		if ((protection & VMM_PROT_EXEC) != 0)
			return -EOPNOTSUPP;
		return -EINVAL;
	}
	*base_out = 0;
	result = vmm_private_map(space, size, &base);
	if (result != 0)
		return result;
	if (protection != (VMM_PROT_READ | VMM_PROT_WRITE)) {
		/* No user return can occur between installation and this change. */
		result = vmm_private_protect(space, base, size, protection);
		if (result != 0) {
			if (vmm_private_unmap(space, base, size) != 0) {
				klog("OpenSillicon panic: protected mapping rollback failed\n");
				kernel_halt();
			}
			/* Failed map operations do not consume the monotonic cursor. */
			space->private_map_cursor = base;
			return result;
		}
	}
	*base_out = base;
	return 0;
}

int
vmm_private_unmap(struct address_space *space, uint64_t base,
	uint64_t requested_size)
{
	struct private_mapping **link;
	struct private_mapping *mapping;
	size_t i;

	if (!vmm_mutation_allowed() || !initialized ||
	    !address_space_valid(space) || space == &kernel_space ||
	    requested_size == 0 || (base & (PAGE_SIZE - 1)) != 0 ||
	    base < PRIVATE_MAP_USABLE_START || base >= PRIVATE_MAP_USABLE_END)
		return -EINVAL;
	mapping = private_mapping_find(space, base, &link);
	if (mapping == NULL || mapping->requested_size != requested_size ||
	    mapping->page_protections == NULL)
		return -EINVAL;
	for (i = 0; i < mapping->page_count; i++) {
		uint64_t virtual_address = base + (uint64_t)i * PAGE_SIZE;
		uint64_t *leaf_table;
		unsigned int leaf_index;
		uint64_t entry;
		uint64_t expected_flags = PTE_PRESENT | PTE_PRIVATE | PTE_NX;
		uint8_t protection = mapping->page_protections[i];

		if (!vmm_private_protection_valid(protection) ||
		    !user_leaf_entry(space, virtual_address, &leaf_table,
		    &leaf_index))
			return -EINVAL;
		if (protection != VMM_PROT_NONE)
			expected_flags |= PTE_USER;
		if ((protection & VMM_PROT_WRITE) != 0)
			expected_flags |= PTE_WRITE;
		entry = leaf_table[leaf_index];
		if ((entry & (PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_PRIVATE |
		    PTE_NX | PTE_SHARED)) != expected_flags ||
		    (entry & PTE_ADDRESS) != mapping->physical_pages[i])
			return -EINVAL;
	}
	for (i = 0; i < mapping->page_count; i++) {
		uint64_t physical;

		if (!unmap_user_page(space, base + (uint64_t)i * PAGE_SIZE,
		    &physical, true) || physical != mapping->physical_pages[i]) {
			klog("OpenSillicon panic: validated private mapping could not unmap\n");
			kernel_halt();
		}
		if (!reclaim_empty_user_tables(space,
		    base + (uint64_t)i * PAGE_SIZE)) {
			klog("OpenSillicon panic: private mapping page-table cleanup failed\n");
			kernel_halt();
		}
	}
	for (i = 0; i < mapping->page_count; i++) {
		if (!pmm_free_page(mapping->physical_pages[i])) {
			klog("OpenSillicon panic: private mapping page release failed\n");
			kernel_halt();
		}
	}
	*link = mapping->next;
	mapping->next = NULL;
	private_mapping_metadata_destroy(mapping);
	return 0;
}

int
vmm_private_unmap_complete(struct address_space *space, uint64_t base,
	uint64_t length)
{
	struct private_mapping *mapping;

	if (!vmm_mutation_allowed() || !initialized ||
	    !address_space_valid(space) || space == &kernel_space ||
	    length == 0 ||
	    (base & (PAGE_SIZE - 1)) != 0 || base < PRIVATE_MAP_USABLE_START ||
	    base >= PRIVATE_MAP_USABLE_END)
		return -EINVAL;
	mapping = private_mapping_find(space, base, NULL);
	if (mapping == NULL || !vmm_private_unmap_length_matches(
	    mapping->mapped_size, length))
		return -EINVAL;
	return vmm_private_unmap(space, base, mapping->requested_size);
}

int
vmm_private_protect(struct address_space *space, uint64_t base,
	uint64_t length, unsigned int protection)
{
	struct private_mapping *mapping;
	uint64_t rounded_length;
	uint64_t end;
	uint64_t offset;
	size_t first_page;
	size_t page_count;
	size_t i;
	bool active_on_any_cpu;
	uint64_t lock_flags;

	if ((protection & VMM_PROT_EXEC) != 0)
		return -EOPNOTSUPP;
	if (!vmm_private_protection_valid(protection))
		return -EINVAL;
	if (!initialized || !address_space_valid(space) || space == &kernel_space ||
	    !vmm_private_protection_range(base, length, &rounded_length, &end) ||
	    base < PRIVATE_MAP_USABLE_START || end > PRIVATE_MAP_USABLE_END)
		return -EINVAL;
	if (base < PRIVATE_MAP_USABLE_START)
		return -EINVAL;
	mapping = space->private_mappings;
	while (mapping != NULL &&
	    (base < mapping->base || base - mapping->base >= mapping->mapped_size))
		mapping = mapping->next;
	if (mapping == NULL || mapping->page_protections == NULL ||
	    mapping->physical_pages == NULL ||
	    mapping->mapped_size > UINT64_MAX - mapping->base)
		return -EINVAL;
	offset = base - mapping->base;
	if (rounded_length > mapping->mapped_size - offset ||
	    end > mapping->base + mapping->mapped_size)
		return -EINVAL;
	first_page = (size_t)(offset / PAGE_SIZE);
	page_count = (size_t)(rounded_length / PAGE_SIZE);
	if ((uint64_t)first_page != offset / PAGE_SIZE || page_count == 0 ||
	    first_page > mapping->page_count ||
	    page_count > mapping->page_count - first_page)
		return -EINVAL;

	/* Validate every leaf before changing any permission or metadata. */
	for (i = 0; i < page_count; i++) {
		uint64_t address = base + (uint64_t)i * PAGE_SIZE;
		uint64_t *leaf_table;
		unsigned int leaf_index;
		uint64_t entry;
		uint8_t current = mapping->page_protections[first_page + i];
		uint64_t expected_flags = PTE_PRESENT | PTE_PRIVATE | PTE_NX;

		if (!vmm_private_protection_valid(current) ||
		    !user_leaf_entry(space, address, &leaf_table, &leaf_index))
			return -EINVAL;
		if (current != VMM_PROT_NONE)
			expected_flags |= PTE_USER;
		if ((current & VMM_PROT_WRITE) != 0)
			expected_flags |= PTE_WRITE;
		entry = leaf_table[leaf_index];
		if ((entry & (PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_PRIVATE |
		    PTE_NX | PTE_SHARED)) != expected_flags ||
		    (entry & PTE_ADDRESS) != mapping->physical_pages[first_page + i])
			return -EINVAL;
	}
	lock_flags = spinlock_lock_irqsave(&address_space_lifecycle_lock);
	active_on_any_cpu = address_space_active_on_any_cpu(space);

	/* The commit loop only rewrites validated leaf entries; it cannot allocate. */
	for (i = 0; i < page_count; i++) {
		uint64_t address = base + (uint64_t)i * PAGE_SIZE;
		uint64_t *leaf_table;
		unsigned int leaf_index;
		uint64_t entry;

		if (!user_leaf_entry(space, address, &leaf_table, &leaf_index)) {
			klog("OpenSillicon panic: validated private protection path changed\n");
			kernel_halt();
		}
		entry = leaf_table[leaf_index] &
		    ~(PTE_USER | PTE_WRITE);
		if (protection != VMM_PROT_NONE)
			entry |= PTE_USER;
		if ((protection & VMM_PROT_WRITE) != 0)
			entry |= PTE_WRITE;
		leaf_table[leaf_index] = entry;
		mapping->page_protections[first_page + i] = (uint8_t)protection;
		if (current_active_space() == space)
			__asm__ volatile ("invlpg (%0)" : :
			    "r" ((void *)(uintptr_t)address) : "memory");
	}
	if (active_on_any_cpu)
		shootdown_tlbs(space);
	spinlock_unlock_irqrestore(&address_space_lifecycle_lock, lock_flags);
	return 0;
}

#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
void
vmm_test_arm_private_map_failure(unsigned int mapped_pages)
{
	test_private_map_fail_after = mapped_pages;
	test_private_map_page_count = 0;
}

void
vmm_test_arm_private_fork_failure(unsigned int copied_pages)
{
	test_private_fork_fail_after = copied_pages;
	test_private_fork_page_count = 0;
}

void
vmm_test_arm_private_metadata_failure(unsigned int allocation_number)
{
	test_private_metadata_fail_at = allocation_number;
	test_private_metadata_allocations = 0;
}

bool
vmm_test_install_private_collision(struct address_space *space)
{
	if (test_private_collision_physical != 0 ||
	    !pmm_alloc_page(&test_private_collision_physical))
		return false;
	if (!zero_physical_page(test_private_collision_physical) ||
	    !vmm_map_page(space, space->private_map_cursor,
	    test_private_collision_physical, VMM_USER | VMM_WRITE | VMM_PRIVATE)) {
		(void)pmm_free_page(test_private_collision_physical);
		test_private_collision_physical = 0;
		return false;
	}
	return true;
}

bool
vmm_test_remove_private_collision(struct address_space *space)
{
	uint64_t physical;
	uint64_t address;

	if (test_private_collision_physical == 0)
		return false;
	address = space->private_map_cursor;
	if (!unmap_user_page(space, address, &physical, true) ||
	    physical != test_private_collision_physical ||
	    !pmm_free_page(physical) ||
	    !reclaim_empty_user_tables(space, address))
		return false;
	test_private_collision_physical = 0;
	return true;
}
#endif

static bool
address_space_create_locked(struct address_space *space)
{
	uint64_t *root;
	uint64_t physical;
	uint64_t *kernel_root;
	uint64_t lock_flags;
	unsigned int i;

	if (!initialized || space == NULL || address_space_valid(space) ||
	    !new_table(&physical, &root))
		return false;
	lock_flags = spinlock_lock_irqsave(&kernel_mapping_lock);
	kernel_root = table_virtual(kernel_space.root_physical);
	if (kernel_root == NULL) {
		spinlock_unlock_irqrestore(&kernel_mapping_lock, lock_flags);
		(void)pmm_free_page(physical);
		return false;
	}
	for (i = 256; i < 512; i++)
		root[i] = kernel_root[i];
	space->root_physical = physical;
	space->shm_next_address = SHM_ARENA_START + PAGE_SIZE;
	shm_mapping_set_init(&space->shared_mappings);
	space->private_map_cursor = PRIVATE_MAP_USABLE_START;
	space->private_mappings = NULL;
	space->next = address_spaces;
	address_spaces = space;
	spinlock_unlock_irqrestore(&kernel_mapping_lock, lock_flags);
	return true;
}

bool
address_space_create(struct address_space *space)
{
	uint64_t lock_flags;
	bool result;

	if (!vmm_mutation_allowed())
		return false;
	lock_flags = spinlock_lock_irqsave(&address_space_lifecycle_lock);
	result = address_space_create_locked(space);
	spinlock_unlock_irqrestore(&address_space_lifecycle_lock, lock_flags);
	return result;
}

bool
address_space_activate(const struct address_space *space)
{
	uint64_t lock_flags;
	bool result = false;

	if (!vmm_mutation_allowed())
		return false;
	lock_flags = spinlock_lock_irqsave(&address_space_lifecycle_lock);
	if (!initialized || !address_space_valid(space) || space->root_physical == 0 ||
	    (space->root_physical & (PAGE_SIZE - 1)) != 0 ||
	    table_virtual(space->root_physical) == NULL)
		goto out;
	__asm__ volatile ("mov %0, %%cr3" : : "r" (space->root_physical) : "memory");
	__atomic_store_n(active_space_slot(), (struct address_space *)space,
	    __ATOMIC_RELEASE);
	result = true;
out:
	spinlock_unlock_irqrestore(&address_space_lifecycle_lock, lock_flags);
	return result;
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
			    (entry & (PTE_SHARED | PTE_PRIVATE)) != 0 ||
			    !validate_user_tables(entry & PTE_ADDRESS, level - 1))
				return false;
		}
	}
	return true;
}

static bool
shared_mapping_page(const struct address_space *space, uint64_t address,
	uint64_t *physical_address)
{
	const struct shm_mapping *mapping;

	for (mapping = space->shared_mappings.head; mapping != NULL;
	    mapping = mapping->next) {
		uint64_t bytes;

		if (mapping->object == NULL || mapping->object->references == 0 ||
		    mapping->object->page_count == 0 ||
		    mapping->object->page_count > UINT64_MAX / PAGE_SIZE ||
		    (mapping->base & (PAGE_SIZE - 1)) != 0)
			return false;
		bytes = (uint64_t)mapping->object->page_count * PAGE_SIZE;
		if (mapping->base < USER_BASE || mapping->base >= USER_END ||
		    bytes > USER_END - mapping->base)
			return false;
		if (address >= mapping->base && address - mapping->base < bytes) {
			uint64_t offset = address - mapping->base;

			if ((offset & (PAGE_SIZE - 1)) != 0)
				return false;
			*physical_address = mapping->object->pages[offset / PAGE_SIZE];
			return true;
		}
	}
	return false;
}

static bool
validate_shared_leaf_tree(const struct address_space *space,
	uint64_t table_physical, unsigned int level, uint64_t virtual_prefix)
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
			uint64_t child_prefix = virtual_prefix |
			    ((uint64_t)i << (level == 4 ? 39 : level == 3 ? 30 : 21));

			if ((entry & (PTE_LARGE | PTE_SHARED | PTE_PRIVATE)) != 0 ||
			    !validate_shared_leaf_tree(space, entry & PTE_ADDRESS,
			    level - 1, child_prefix))
				return false;
		} else if ((entry & PTE_SHARED) != 0) {
			uint64_t expected_physical;
			uint64_t virtual_address = virtual_prefix |
			    ((uint64_t)i << 12);

			if ((entry & (PTE_USER | PTE_WRITE | PTE_NX)) !=
			    (PTE_USER | PTE_WRITE | PTE_NX) ||
			    !shared_mapping_page(space, virtual_address,
			    &expected_physical) ||
			    expected_physical != (entry & PTE_ADDRESS))
				return false;
		}
	}
	return true;
}

static bool
private_mapping_page(const struct address_space *space, uint64_t address,
	uint64_t *physical_address, uint8_t *protection)
{
	const struct private_mapping *mapping;

	for (mapping = space->private_mappings; mapping != NULL;
	    mapping = mapping->next) {
		uint64_t offset;

		if (address < mapping->base || address - mapping->base >=
		    mapping->mapped_size)
			continue;
		offset = address - mapping->base;
		if ((offset & (PAGE_SIZE - 1)) != 0 ||
		    offset / PAGE_SIZE >= mapping->page_count)
			return false;
		if (mapping->physical_pages == NULL ||
		    mapping->page_protections == NULL)
			return false;
		*physical_address = mapping->physical_pages[offset / PAGE_SIZE];
		if (protection != NULL)
			*protection = mapping->page_protections[offset / PAGE_SIZE];
		return true;
	}
	return false;
}

static bool
validate_private_leaf_tree(const struct address_space *space,
	uint64_t table_physical, unsigned int level, uint64_t virtual_prefix)
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
			uint64_t child_prefix = virtual_prefix |
			    ((uint64_t)i << (level == 4 ? 39 : level == 3 ? 30 : 21));

			if ((entry & (PTE_LARGE | PTE_SHARED | PTE_PRIVATE)) != 0 ||
			    !validate_private_leaf_tree(space, entry & PTE_ADDRESS,
			    level - 1, child_prefix))
				return false;
		} else if ((entry & PTE_PRIVATE) != 0) {
			uint64_t expected_physical;
			uint64_t address = virtual_prefix | ((uint64_t)i << 12);
			uint8_t protection;
			uint64_t expected_flags = PTE_PRESENT | PTE_PRIVATE | PTE_NX;

			if (!private_mapping_page(space, address, &expected_physical,
			    &protection) || !vmm_private_protection_valid(protection))
				return false;
			if (protection != VMM_PROT_NONE)
				expected_flags |= PTE_USER;
			if ((protection & VMM_PROT_WRITE) != 0)
				expected_flags |= PTE_WRITE;
			if ((entry & (PTE_PRESENT | PTE_PRIVATE | PTE_NX | PTE_USER |
			    PTE_WRITE | PTE_SHARED)) != expected_flags ||
			    expected_physical != (entry & PTE_ADDRESS))
				return false;
		}
	}
	return true;
}

static bool
validate_private_mappings(const struct address_space *space)
{
	const struct private_mapping *mapping;
	const struct private_mapping *previous = NULL;
	uint64_t *root = table_virtual(space->root_physical);
	unsigned int i;

	if (root == NULL ||
	    space->private_map_cursor < PRIVATE_MAP_USABLE_START ||
	    space->private_map_cursor > PRIVATE_MAP_USABLE_END ||
	    (space->private_map_cursor & (PAGE_SIZE - 1)) != 0)
		return false;
	for (mapping = space->private_mappings; mapping != NULL;
	    mapping = mapping->next) {
		uint64_t rounded;
		size_t page_index;

		if (mapping->requested_size == 0 ||
		    mapping->requested_size > UINT64_MAX - (PAGE_SIZE - 1) ||
		    mapping->base < PRIVATE_MAP_USABLE_START ||
		    mapping->base >= PRIVATE_MAP_USABLE_END ||
		    (mapping->base & (PAGE_SIZE - 1)) != 0)
			return false;
		rounded = (mapping->requested_size + PAGE_SIZE - 1) &
		    ~(PAGE_SIZE - 1);
		if (rounded != mapping->mapped_size ||
		    rounded > PRIVATE_MAP_USABLE_END - mapping->base ||
		    mapping->base > space->private_map_cursor ||
		    rounded > space->private_map_cursor - mapping->base ||
		    mapping->physical_pages == NULL ||
		    mapping->page_protections == NULL ||
		    mapping->page_count != rounded / PAGE_SIZE)
			return false;
		if (previous != NULL && (previous->base >= mapping->base ||
		    previous->mapped_size > mapping->base - previous->base))
			return false;
		for (page_index = 0; page_index < mapping->page_count; page_index++) {
			uint64_t address = mapping->base + page_index * PAGE_SIZE;
			uint64_t *leaf_table;
			unsigned int leaf_index;
			uint64_t entry;
			uint8_t protection = mapping->page_protections[page_index];
			uint64_t expected_flags = PTE_PRESENT | PTE_PRIVATE | PTE_NX;

			if (!vmm_private_protection_valid(protection) ||
			    !user_leaf_entry(space, address, &leaf_table, &leaf_index))
				return false;
			entry = leaf_table[leaf_index];
			if (protection != VMM_PROT_NONE)
				expected_flags |= PTE_USER;
			if ((protection & VMM_PROT_WRITE) != 0)
				expected_flags |= PTE_WRITE;
			if ((entry & (PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_PRIVATE |
			    PTE_NX | PTE_SHARED)) != expected_flags ||
			    (entry & PTE_ADDRESS) != mapping->physical_pages[page_index])
				return false;
		}
		previous = mapping;
	}
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    !validate_private_leaf_tree(space, entry & PTE_ADDRESS, 3,
		    (uint64_t)i << 39))
			return false;
	}
	return true;
}

static bool
validate_shared_mappings(const struct address_space *space)
{
	const struct shm_mapping *mapping;
	uint64_t *root = table_virtual(space->root_physical);
	unsigned int i;

	if (root == NULL ||
	    space->shm_next_address < SHM_ARENA_START + PAGE_SIZE ||
	    space->shm_next_address > SHM_ARENA_END - PAGE_SIZE ||
	    (space->shm_next_address & (PAGE_SIZE - 1)) != 0)
		return false;
	for (mapping = space->shared_mappings.head; mapping != NULL;
	    mapping = mapping->next) {
		size_t page_index;

		if (mapping->object == NULL || mapping->object->references == 0 ||
		    mapping->object->page_count == 0 ||
		    mapping->object->page_count > UINT64_MAX / PAGE_SIZE ||
		    mapping->base < USER_BASE || mapping->base >= USER_END ||
		    (mapping->base & (PAGE_SIZE - 1)) != 0 ||
		    mapping->object->page_count * PAGE_SIZE > USER_END - mapping->base)
			return false;
		if (mapping->base >= space->shm_next_address ||
		    mapping->object->page_count * PAGE_SIZE >
		    space->shm_next_address - mapping->base ||
		    PAGE_SIZE > space->shm_next_address - mapping->base -
		    mapping->object->page_count * PAGE_SIZE)
			return false;
		for (page_index = 0; page_index < mapping->object->page_count;
		    page_index++) {
			uint64_t address = mapping->base +
			    (uint64_t)page_index * PAGE_SIZE;
			uint64_t *leaf_table;
			unsigned int leaf_index;
			uint64_t entry;

			if (!user_leaf_entry(space, address,
			    &leaf_table, &leaf_index))
				return false;
			entry = leaf_table[leaf_index];
			if ((entry & (PTE_PRESENT | PTE_USER | PTE_WRITE |
			    PTE_SHARED | PTE_NX)) !=
			    (PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_SHARED | PTE_NX) ||
			    (entry & PTE_ADDRESS) != mapping->object->pages[page_index])
				return false;
		}
	}
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    !validate_shared_leaf_tree(space, entry & PTE_ADDRESS, 3,
		    (uint64_t)i << 39))
			return false;
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
	if (!pmm_free_page(table_physical)) {
		klog("OpenSillicon panic: failed to free user page table\n");
		kernel_halt();
	}
}

static bool
destroy_address_space_tables(struct address_space *space)
{
	uint64_t *root;
	uint64_t kernel_lock_flags;
	unsigned int i;

	if (!initialized || !address_space_valid(space) || space->root_physical == 0 ||
	    space->root_physical == kernel_space.root_physical ||
	    address_space_active_on_any_cpu(space) ||
	    space->root_physical == read_cr3() ||
	    space->private_mappings != NULL)
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
	{
		uint64_t root_physical = space->root_physical;
		struct address_space **link = &address_spaces;

		kernel_lock_flags = spinlock_lock_irqsave(&kernel_mapping_lock);
		while (*link != NULL && *link != space)
			link = &(*link)->next;
		if (*link != space || !pmm_free_page(root_physical)) {
			spinlock_unlock_irqrestore(&kernel_mapping_lock,
			    kernel_lock_flags);
			return false;
		}
		*link = space->next;
		spinlock_unlock_irqrestore(&kernel_mapping_lock, kernel_lock_flags);
	}
	space->root_physical = 0;
	space->shm_next_address = 0;
	space->private_map_cursor = 0;
	space->private_mappings = NULL;
	space->next = NULL;
	return true;
}

static bool
address_space_destroy_locked(struct address_space *space)
{
	uint64_t *root;
	unsigned int i;

	if (!initialized || space == NULL || !address_space_valid(space) ||
	    space->root_physical == 0 ||
	    space->shared_mappings.head != NULL ||
	    space->private_mappings != NULL)
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
	if (!validate_shared_mappings(space) || !validate_private_mappings(space))
		return false;
	return destroy_address_space_tables(space);
}

bool
address_space_destroy(struct address_space *space)
{
	uint64_t lock_flags;
	bool result;

	if (!vmm_mutation_allowed())
		return false;
	lock_flags = spinlock_lock_irqsave(&address_space_lifecycle_lock);
	result = address_space_destroy_locked(space);

	spinlock_unlock_irqrestore(&address_space_lifecycle_lock, lock_flags);
	return result;
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
		const void *input;

		if (chunk > remaining)
			chunk = remaining;
		if (!user_page_physical(space, current, &physical, false, false) ||
		    hhdm_offset > UINT64_MAX - physical ||
		    chunk - 1 > UINTPTR_MAX - (uintptr_t)(hhdm_offset + physical +
		    (current & (PAGE_SIZE - 1))))
			return false;
		input = (const void *)(uintptr_t)(hhdm_offset + physical +
		    (current & (PAGE_SIZE - 1)));
		x86_copy_ram((void *)output, input, (size_t)chunk);
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
vmm_validate_user_read(const struct address_space *space,
	uint64_t source, size_t length)
{
	uint64_t current;
	uint64_t remaining;

	if (!initialized || !address_space_valid(space) || space == &kernel_space)
		return false;
	if (length == 0)
		return true;
	if (source < USER_BASE || source >= USER_END ||
	    length > USER_END - source)
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
clone_user_tables(const struct address_space *source, uint64_t table_physical,
	unsigned int level, uint64_t virtual_prefix,
	struct address_space *destination)
{
	uint64_t *table = table_virtual(table_physical);
	unsigned int i;

	if (table == NULL)
		return false;
	for (i = 0; i < 512; i++) {
		uint64_t entry = table[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if ((entry & PTE_LARGE) != 0 ||
		    (level > 1 && ((entry & PTE_USER) == 0 ||
		    (entry & PTE_SHARED) != 0)) ||
		    (level == 1 && (entry & PTE_USER) == 0 &&
		    (entry & PTE_PRIVATE) == 0))
			return false;
		if (level > 1) {
			if (!clone_user_tables(source, entry & PTE_ADDRESS, level - 1,
			    virtual_prefix | ((uint64_t)i << (level == 3 ? 30 : 21)),
			    destination))
				return false;
		} else {
			uint64_t virtual_address = virtual_prefix |
			    ((uint64_t)i << 12);
			uint64_t copy_physical;
			unsigned int flags = VMM_USER;
			uint8_t protection = VMM_PROT_READ | VMM_PROT_WRITE;

			if ((entry & PTE_SHARED) != 0) {
				if ((entry & (PTE_WRITE | PTE_NX)) !=
				    (PTE_WRITE | PTE_NX))
					return false;
				continue;
			}
			if ((entry & PTE_PRIVATE) != 0) {
				uint64_t expected_physical;

				if (!private_mapping_page(source, virtual_address,
				    &expected_physical, &protection) ||
				    expected_physical != (entry & PTE_ADDRESS) ||
				    !vmm_private_protection_valid(protection) ||
				    (entry & (PTE_WRITE | PTE_NX | PTE_SHARED)) !=
				    (((protection & VMM_PROT_WRITE) != 0 ? PTE_WRITE : 0) |
				    PTE_NX) ||
				    (((entry & PTE_USER) != 0) !=
				    (protection != VMM_PROT_NONE)))
					return false;
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
				if (test_private_fork_fail_after != 0 &&
				    test_private_fork_page_count >=
				    test_private_fork_fail_after) {
					test_private_fork_fail_after = 0;
					klog("OpenSillicon test: injected private fork failure\n");
					return false;
				}
#endif
				flags |= VMM_PRIVATE;
			}

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
			if ((entry & PTE_PRIVATE) != 0 &&
			    protection == VMM_PROT_NONE) {
				uint64_t *leaf_table;
				unsigned int leaf_index;

				if (!user_leaf_entry(destination, virtual_address,
				    &leaf_table, &leaf_index))
					return false;
				leaf_table[leaf_index] &= ~PTE_USER;
			}
#ifdef KERNEL_TEST_PRIVATE_MAPPING_FAILURE
			if ((entry & PTE_PRIVATE) != 0)
				test_private_fork_page_count++;
#endif
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
			table[i] = 0;
			/* Teardown only accepts inactive address spaces. With PCID and SMP
			 * disabled, switching CR3 has already invalidated their translations. */
			__asm__ volatile ("" : : : "memory");
			if ((entry & PTE_SHARED) == 0 &&
			    !pmm_free_page(entry & PTE_ADDRESS))
				return false;
		}
	}
	return true;
}

static bool
clone_shared_map_page(void *context, uint64_t virtual_address,
	uint64_t physical_address)
{
#ifdef KERNEL_TEST_SHM_FORK_FAILURE
	if (test_shm_fork_failure_armed) {
		if (test_shm_fork_mapped_pages++ == 1) {
			test_shm_fork_failure_armed = false;
			klog("OpenSillicon test: injected shared fork mapping failure\n");
			return false;
		}
	}
#endif
	return map_shared_page(context, virtual_address, physical_address);
}

#ifdef KERNEL_TEST_SHM_FORK_FAILURE
void
vmm_test_arm_shm_fork_failure(void)
{
	test_shm_fork_mapped_pages = 0;
	test_shm_fork_failure_armed = true;
}
#endif

static bool
address_space_destroy_with_pages_locked(struct address_space *space)
{
	uint64_t *root;
	unsigned int i;

	if (!initialized || !address_space_valid(space) || space == &kernel_space ||
	    space->root_physical == 0 ||
	    address_space_active_on_any_cpu(space) ||
	    space->root_physical == read_cr3())
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
	if (!validate_shared_mappings(space) || !validate_private_mappings(space))
		return false;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    !free_user_leaves(entry & PTE_ADDRESS, 3))
			return false;
	}
	private_mapping_metadata_destroy(space->private_mappings);
	space->private_mappings = NULL;
	if (!destroy_address_space_tables(space))
		return false;
	__asm__ volatile ("" : : : "memory");
	shm_mapping_set_destroy(&space->shared_mappings);
	return true;
}

bool
address_space_destroy_with_pages(struct address_space *space)
{
	uint64_t lock_flags;
	bool result;

	if (!vmm_mutation_allowed())
		return false;
	lock_flags = spinlock_lock_irqsave(&address_space_lifecycle_lock);
	result = address_space_destroy_with_pages_locked(space);

	spinlock_unlock_irqrestore(&address_space_lifecycle_lock, lock_flags);
	return result;
}

static bool
private_mapping_set_clone(const struct private_mapping *source,
	struct address_space *destination_space)
{
	struct private_mapping **tail = &destination_space->private_mappings;

	destination_space->private_mappings = NULL;
	while (source != NULL) {
		struct private_mapping *copy = kheap_alloc(sizeof(*copy));
		size_t i;

		if (copy == NULL)
			return false;
		copy->physical_pages = kheap_alloc(source->page_count *
		    sizeof(*copy->physical_pages));
		if (copy->physical_pages == NULL) {
			(void)kheap_free(copy);
			return false;
		}
		copy->base = source->base;
		copy->requested_size = source->requested_size;
		copy->mapped_size = source->mapped_size;
		copy->page_count = source->page_count;
		copy->next = NULL;
		copy->page_protections = kheap_alloc(source->page_count);
		if (copy->page_protections == NULL) {
			(void)kheap_free(copy->physical_pages);
			(void)kheap_free(copy);
			return false;
		}
		for (i = 0; i < source->page_count; i++)
			copy->page_protections[i] = source->page_protections[i];
		for (i = 0; i < source->page_count; i++) {
			uint64_t address = source->base + i * PAGE_SIZE;
			uint64_t *leaf_table;
			unsigned int leaf_index;
			uint64_t entry;
			uint8_t protection = source->page_protections[i];
			uint64_t expected_flags = PTE_PRESENT | PTE_PRIVATE | PTE_NX;

			if (!user_leaf_entry(destination_space, address, &leaf_table,
			    &leaf_index)) {
				(void)kheap_free(copy->page_protections);
				(void)kheap_free(copy->physical_pages);
				(void)kheap_free(copy);
				return false;
			}
			entry = leaf_table[leaf_index];
			if (!vmm_private_protection_valid(protection)) {
				(void)kheap_free(copy->page_protections);
				(void)kheap_free(copy->physical_pages);
				(void)kheap_free(copy);
				return false;
			}
			if (protection != VMM_PROT_NONE)
				expected_flags |= PTE_USER;
			if ((protection & VMM_PROT_WRITE) != 0)
				expected_flags |= PTE_WRITE;
			if ((entry & (PTE_PRESENT | PTE_PRIVATE | PTE_USER | PTE_WRITE |
			    PTE_NX)) != expected_flags) {
				(void)kheap_free(copy->page_protections);
				(void)kheap_free(copy->physical_pages);
				(void)kheap_free(copy);
				return false;
			}
			copy->physical_pages[i] = entry & PTE_ADDRESS;
		}
		*tail = copy;
		tail = &copy->next;
		source = source->next;
	}
	return true;
}

static bool
rollback_address_space_clone(struct address_space *space)
{
	uint64_t *root = table_virtual(space->root_physical);
	unsigned int i;

	if (root == NULL || space->shared_mappings.head != NULL)
		return false;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    !free_user_leaves(entry & PTE_ADDRESS, 3))
			return false;
	}
	private_mapping_metadata_destroy(space->private_mappings);
	space->private_mappings = NULL;
	if (!destroy_address_space_tables(space))
		return false;
	shm_mapping_set_destroy(&space->shared_mappings);
	return true;
}

static bool
address_space_clone_locked(const struct address_space *source,
	struct address_space *destination)
{
	uint64_t *root;
	unsigned int i;

	if (!initialized || source == NULL || source == &kernel_space ||
	    !address_space_valid(source) || destination == NULL)
		return false;
	root = table_virtual(source->root_physical);
	if (root == NULL)
		return false;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) != 0 &&
		    ((entry & PTE_LARGE) != 0 ||
		    !validate_user_tables(entry & PTE_ADDRESS, 3)))
			return false;
	}
	if (!validate_shared_mappings(source)) {
		klog("OpenSillicon: private fork source shared validation failed\n");
		return false;
	}
	if (!validate_private_mappings(source)) {
		klog("OpenSillicon: private fork source metadata validation failed\n");
		return false;
	}
	if (!address_space_create_locked(destination))
		return false;
	destination->shm_next_address = source->shm_next_address;
	destination->private_map_cursor = source->private_map_cursor;
	for (i = 0; i < 256; i++) {
		uint64_t entry = root[i];

		if ((entry & PTE_PRESENT) == 0)
			continue;
		if ((entry & PTE_USER) == 0 || (entry & PTE_LARGE) != 0 ||
		    !clone_user_tables(source, entry & PTE_ADDRESS, 3,
		    (uint64_t)i << 39, destination)) {
			klog("OpenSillicon: private fork page clone failed\n");
			goto fail;
		}
	}
	if (!private_mapping_set_clone(source->private_mappings,
	    destination)) {
		klog("OpenSillicon: private fork metadata clone failed\n");
		goto fail;
	}
	if (!shm_mapping_set_clone(&source->shared_mappings,
	    &destination->shared_mappings, clone_shared_map_page,
	    unmap_shared_page, destination))
		goto fail;
	return true;

fail:
	if (!rollback_address_space_clone(destination))
		return false;
	return false;
}

bool
address_space_clone(const struct address_space *source,
	struct address_space *destination)
{
	uint64_t lock_flags;
	bool result;

	if (!vmm_mutation_allowed())
		return false;
	lock_flags = spinlock_lock_irqsave(&address_space_lifecycle_lock);
	result = address_space_clone_locked(source, destination);

	spinlock_unlock_irqrestore(&address_space_lifecycle_lock, lock_flags);
	return result;
}

uint64_t
vmm_hhdm_offset(void)
{
	return initialized ? hhdm_offset : 0;
}

struct address_space *
vmm_active_address_space(void)
{
	return initialized ? current_active_space() : NULL;
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

uint64_t
vmm_vmalloc_device_start(void)
{
	return VMALLOC_END - VMM_DEVICE_WINDOW_SIZE;
}

uint64_t
vmm_vmalloc_device_end(void)
{
	return VMALLOC_END;
}

uint64_t
vmm_module_arena_start(void)
{
	return VMM_MODULE_ARENA_START;
}

uint64_t
vmm_module_arena_end(void)
{
	return VMM_MODULE_ARENA_END;
}

bool
vmm_physical_range_mmio_safe(uint64_t physical_address, uint64_t length)
{
	uint64_t end;
	uint64_t cursor;
	bool entirely_reserved = true;

	if (!initialized || physical_memory_map == NULL || length == 0 ||
	    physical_address >= (1ULL << 52) ||
	    length > (1ULL << 52) - physical_address)
		return false;
	end = physical_address + length;
	cursor = physical_address;
	while (cursor < end) {
		const struct limine_memmap_entry *cover = NULL;
		uint64_t cover_end = 0;

		for (uint64_t i = 0; i < physical_memory_map->entry_count; i++) {
			const struct limine_memmap_entry *entry =
			    physical_memory_map->entries[i];
			uint64_t entry_end;

			if (entry == NULL || entry->length == 0 ||
			    entry->base > UINT64_MAX - entry->length)
				return false;
			entry_end = entry->base + entry->length;
			if (cursor >= entry->base && cursor < entry_end) {
				if (cover != NULL)
					return false;
				cover = entry;
				cover_end = entry_end;
			}
			if (entry->type != LIMINE_MEMMAP_RESERVED &&
			    entry->base < end && physical_address < entry_end)
				return false;
		}
		if (cover == NULL) {
			entirely_reserved = false;
			break;
		}
		if (cover->type != LIMINE_MEMMAP_RESERVED)
			entirely_reserved = false;
		cursor = cover_end < end ? cover_end : end;
	}
	/* Firmware does not always describe PCI apertures in the memory map. It is
	 * safe to map such a BAR only when the complete range is a hole (no Limine
	 * entry of any type overlaps it). Partially described ranges are rejected. */
	if (entirely_reserved)
		return true;
	for (uint64_t i = 0; i < physical_memory_map->entry_count; i++) {
		const struct limine_memmap_entry *entry =
		    physical_memory_map->entries[i];
		uint64_t entry_end;

		if (entry == NULL || entry->length == 0 ||
		    entry->base > UINT64_MAX - entry->length)
			return false;
		entry_end = entry->base + entry->length;
		if (entry->base < end && physical_address < entry_end)
			return false;
	}
	return true;
}
