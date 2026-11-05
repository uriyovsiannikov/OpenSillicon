#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/heap.h"
#include "kernel/fd.h"
#include "kernel/module.h"
#include "kernel/pmm.h"
#include "kernel/spinlock.h"
#include "kernel/vfs.h"
#include "kernel/vmm.h"

#define PAGE_SIZE 4096U
#define ELF_HEADER_SIZE 64U
#define ELF_SECTION_SIZE 64U
#define ELF_SYMBOL_SIZE 24U
#define ELF_ET_REL 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define SHT_NOBITS 8U
#define SHF_ALLOC 0x2U
#define SHF_WRITE 0x1U
#define SHF_EXECINSTR 0x4U
#define SHN_UNDEF 0U
#define SHN_ABS 0xfff1U
#define STT_FUNC 2U
#define MODULE_LIMIT KERNEL_MODULE_LIMIT
#define MODULE_SLOT_SIZE (VMM_MODULE_ARENA_END - VMM_MODULE_ARENA_START) / MODULE_LIMIT
#define MODULE_MAX_SIZE MODULE_SLOT_SIZE

struct loaded_module {
	bool busy;
	bool loaded;
	uint32_t generation;
	uint32_t references;
	char name[KERNEL_MODULE_NAME_MAX + 1U];
	size_t page_count;
	uint64_t *physical_pages;
	uint8_t *page_flags;
	uint8_t *page_mapped;
	struct kernel_module_section_memory *sections;
	void (*fini)(void);
	int (*prepare_unload)(void);
};

static struct loaded_module modules[MODULE_LIMIT];
static struct spinlock module_lock = SPINLOCK_INITIALIZER;
static uint32_t next_generation = 1;
static bool name_valid(const char *name, size_t *length);

static bool
symbol_name_is(const uint8_t *strings, size_t size, uint32_t offset,
	const char *name)
{
	size_t i = 0;
	while (offset + i < size && name[i] != '\0' &&
	    strings[offset + i] == (uint8_t)name[i])
		i++;
	return offset + i < size && name[i] == '\0' && strings[offset + i] == 0;
}

enum kernel_module_error
kernel_module_load_path(const char *name, const char *path)
{
	struct fd_metadata stat;
	struct fd_object *file = NULL;
	void *image;
	size_t offset = 0;
	size_t name_length;
	int error;
	enum kernel_module_error result;
	if (!name_valid(name, &name_length) || path == NULL)
		return KERNEL_MODULE_INVALID_ARGUMENT;
	error = vfs_open(path, VFS_O_RDONLY, 0, &file);
	if (error != 0)
		return error == -2 ? KERNEL_MODULE_NOT_FOUND : KERNEL_MODULE_IO_ERROR;
	if (fd_object_stat(file, &stat) != 0) {
		fd_object_put(file);
		return KERNEL_MODULE_IO_ERROR;
	}
	if (stat.type != VNODE_REGULAR || stat.size == 0 ||
	    stat.size > 8U * 1024U * 1024U || stat.size > SIZE_MAX) {
		fd_object_put(file);
		return KERNEL_MODULE_INVALID_ARGUMENT;
	}
	image = kheap_alloc((size_t)stat.size);
	if (image == NULL) {
		fd_object_put(file);
		return KERNEL_MODULE_NO_MEMORY;
	}
	while (offset < stat.size) {
		size_t amount = 0;
		if (fd_object_read(file, (uint8_t *)image + offset,
		    (size_t)stat.size - offset, &amount) != 0 || amount == 0 ||
		    amount > stat.size - offset)
			break;
		offset += amount;
	}
	fd_object_put(file);
	result = offset == stat.size ? kernel_module_load(name, image, offset) :
	    KERNEL_MODULE_IO_ERROR;
	(void)kheap_free(image);
	return result;
}

static uint16_t
read16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t
read32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
read64(const uint8_t *p)
{
	return (uint64_t)read32(p) | (uint64_t)read32(p + 4) << 32;
}

static bool
name_valid(const char *name, size_t *length)
{
	size_t size = 0;
	if (name == NULL)
		return false;
	while (size <= KERNEL_MODULE_NAME_MAX && name[size] != '\0') {
		unsigned char c = (unsigned char)name[size];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
			return false;
		size++;
	}
	if (size == 0 || size > KERNEL_MODULE_NAME_MAX)
		return false;
	*length = size;
	return true;
}

static bool
module_name_equal(const struct loaded_module *module, const char *name)
{
	for (size_t i = 0; i <= KERNEL_MODULE_NAME_MAX; i++) {
		if (module->name[i] != name[i])
			return false;
		if (name[i] == '\0')
			return true;
	}
	return false;
}

static bool
unmap_and_free(struct loaded_module *module, size_t slot)
{
	uint64_t base = VMM_MODULE_ARENA_START + slot * MODULE_SLOT_SIZE;
	bool success = true;
	if (module->physical_pages != NULL) {
		for (size_t i = 0; i < module->page_count; i++) {
			if (module->page_mapped != NULL && module->page_mapped[i]) {
				uint64_t physical;
				if (!vmm_unmap_page(vmm_kernel_address_space(),
				    base + i * PAGE_SIZE, &physical) ||
				    physical != module->physical_pages[i]) {
					success = false;
					continue;
				}
				module->page_mapped[i] = 0;
			}
			if (module->physical_pages[i] != 0) {
				if (!pmm_free_page(module->physical_pages[i]))
					success = false;
				else
					module->physical_pages[i] = 0;
			}
		}
	}
	if (success) {
		if (module->physical_pages != NULL)
			(void)kheap_free(module->physical_pages);
		if (module->page_flags != NULL)
			(void)kheap_free(module->page_flags);
		if (module->page_mapped != NULL)
			(void)kheap_free(module->page_mapped);
		if (module->sections != NULL)
			(void)kheap_free(module->sections);
		{
			size_t slot = (size_t)(module - modules);
			uint64_t flags = spinlock_lock_irqsave(&module_lock);
			modules[slot] = (struct loaded_module){0};
			spinlock_unlock_irqrestore(&module_lock, flags);
		}
	}
	return success;
}

static bool
find_entry_points(const uint8_t *image, size_t image_size,
	const struct kernel_module_section_memory *sections, size_t section_count,
	int (**init)(void), void (**fini)(void), int (**prepare_unload)(void))
{
	uint64_t shoff = read64(image + 40);
	uint16_t shnum = read16(image + 60);
	*init = NULL;
	*fini = NULL;
	*prepare_unload = NULL;
	if (shnum > section_count || shnum > KERNEL_MODULE_MAX_SECTIONS)
		return false;
	for (size_t i = 0; i < shnum; i++) {
		const uint8_t *symbol_section = image + shoff + i * ELF_SECTION_SIZE;
		uint32_t type = read32(symbol_section + 4);
		uint64_t offset = read64(symbol_section + 24);
		uint64_t size = read64(symbol_section + 32);
		uint32_t link = read32(symbol_section + 40);
		uint64_t entsize = read64(symbol_section + 56);
		const uint8_t *strings;
		uint64_t strings_size;
		if (type != SHT_SYMTAB)
			continue;
		if (link >= shnum || entsize != ELF_SYMBOL_SIZE ||
		    size % ELF_SYMBOL_SIZE != 0 || offset > image_size ||
		    size > image_size - offset)
			return false;
		{
			const uint8_t *strhdr = image + shoff +
			    (uint64_t)link * ELF_SECTION_SIZE;
			if (read32(strhdr + 4) != SHT_STRTAB)
				return false;
			strings = image + read64(strhdr + 24);
			strings_size = read64(strhdr + 32);
			if ((uint64_t)(strings - image) > image_size ||
			    strings_size > image_size - (uint64_t)(strings - image))
				return false;
		}
		for (size_t j = 0; j < size / ELF_SYMBOL_SIZE; j++) {
			const uint8_t *symbol = image + offset + j * ELF_SYMBOL_SIZE;
			uint32_t name_offset = read32(symbol);
			uint8_t symbol_type = symbol[4] & 0xfU;
			uint16_t section_index = read16(symbol + 6);
			uint64_t value = read64(symbol + 8);
			uint64_t symbol_size = read64(symbol + 16);
			const uint8_t *section_header;
			uint64_t section_flags;
			bool is_init;
			if (name_offset >= strings_size || symbol_type != STT_FUNC ||
			    section_index == SHN_UNDEF || section_index == SHN_ABS ||
			    section_index >= shnum || sections[section_index].address == NULL)
				continue;
			section_header = image + shoff +
			    (uint64_t)section_index * ELF_SECTION_SIZE;
			section_flags = read64(section_header + 8);
			if ((section_flags & SHF_EXECINSTR) == 0 ||
			    value >= sections[section_index].size ||
			    symbol_size > sections[section_index].size ||
			    value > sections[section_index].size - symbol_size)
				continue;
			is_init = strings_size - name_offset >= 12 &&
			    strings[name_offset] == 'm' && strings[name_offset + 1] == 'o' &&
			    strings[name_offset + 2] == 'd' && strings[name_offset + 3] == 'u' &&
			    strings[name_offset + 4] == 'l' && strings[name_offset + 5] == 'e' &&
			    strings[name_offset + 6] == '_' && strings[name_offset + 7] == 'i' &&
			    strings[name_offset + 8] == 'n' && strings[name_offset + 9] == 'i' &&
			    strings[name_offset + 10] == 't' && strings[name_offset + 11] == 0;
			if (value >= sections[section_index].size ||
			    value > UINTPTR_MAX - (uintptr_t)sections[section_index].address)
				continue;
			if (is_init && *init == NULL)
				*init = (int (*)(void))((uintptr_t)sections[section_index].address + value);
			else if (symbol_name_is(strings, strings_size, name_offset,
			    "module_prepare_unload") && *prepare_unload == NULL)
				*prepare_unload = (int (*)(void))((uintptr_t)sections[section_index].address + value);
			else if (!is_init && strings_size - name_offset >= 12 &&
			    strings[name_offset] == 'm' && strings[name_offset + 1] == 'o' &&
			    strings[name_offset + 2] == 'd' && strings[name_offset + 3] == 'u' &&
			    strings[name_offset + 4] == 'l' && strings[name_offset + 5] == 'e' &&
			    strings[name_offset + 6] == '_' && strings[name_offset + 7] == 'f' &&
			    strings[name_offset + 8] == 'i' && strings[name_offset + 9] == 'n' &&
			    strings[name_offset + 10] == 'i' && strings[name_offset + 11] == 0 &&
			    *fini == NULL)
				*fini = (void (*)(void))((uintptr_t)sections[section_index].address + value);
		}
	}
	return *init != NULL;
}

enum kernel_module_error
kernel_module_load(const char *name, const void *object, size_t image_size)
{
	const uint8_t *image = object;
	struct kernel_module_info info;
	struct loaded_module *module = NULL;
	struct kernel_module_section_memory *sections = NULL;
	size_t name_length;
	size_t shnum;
	size_t cursor = 0;
	size_t page_count;
	uint64_t base;
	uint64_t shoff;
	size_t slot;
	int (*init)(void) = NULL;
	void (*fini)(void) = NULL;
	int (*prepare_unload)(void) = NULL;
	enum kernel_module_error error;
	uint64_t lock_flags;
	if (!name_valid(name, &name_length) || image == NULL)
		return KERNEL_MODULE_INVALID_ARGUMENT;
	error = kernel_module_validate(image, image_size, &info);
	if (error != KERNEL_MODULE_OK)
		return error;
	shnum = read16(image + 60);
	shoff = read64(image + 40);
	for (size_t i = 0; i < shnum; i++) {
		const uint8_t *section = image + shoff + i * ELF_SECTION_SIZE;
		uint64_t flags = read64(section + 8);
		uint64_t size = read64(section + 32);
		uint64_t alignment = read64(section + 48);
		if ((flags & SHF_ALLOC) == 0 || size == 0)
			continue;
		if (alignment < PAGE_SIZE)
			alignment = PAGE_SIZE;
		if (alignment > MODULE_MAX_SIZE ||
		    cursor > SIZE_MAX - (size_t)(alignment - 1U))
			return KERNEL_MODULE_NO_MEMORY;
		cursor = (cursor + (size_t)alignment - 1U) & ~((size_t)alignment - 1U);
		if (size > MODULE_MAX_SIZE - cursor)
			return KERNEL_MODULE_NO_MEMORY;
		cursor += (size_t)size;
	}
	if (cursor == 0)
		return KERNEL_MODULE_INVALID_ELF;
	page_count = (cursor + PAGE_SIZE - 1U) / PAGE_SIZE;
	if (page_count > MODULE_MAX_SIZE / PAGE_SIZE)
		return KERNEL_MODULE_NO_MEMORY;
	lock_flags = spinlock_lock_irqsave(&module_lock);
	for (slot = 0; slot < MODULE_LIMIT; slot++) {
		if (modules[slot].busy && module_name_equal(&modules[slot], name)) {
			spinlock_unlock_irqrestore(&module_lock, lock_flags);
			return KERNEL_MODULE_ALREADY_LOADED;
		}
		if (!modules[slot].busy && module == NULL) {
			module = &modules[slot];
		}
	}
	if (module == NULL) {
		spinlock_unlock_irqrestore(&module_lock, lock_flags);
		return KERNEL_MODULE_NO_MEMORY;
	}
	slot = (size_t)(module - modules);
	*module = (struct loaded_module){.busy = true};
	module->generation = next_generation++;
	if (module->generation == 0) {
		module->generation = next_generation++;
		if (module->generation == 0)
			module->generation = 1;
	}
	for (size_t i = 0; i < name_length; i++)
		module->name[i] = name[i];
	module->name[name_length] = '\0';
	spinlock_unlock_irqrestore(&module_lock, lock_flags);
	module->page_count = page_count;
	module->physical_pages = kheap_alloc(page_count * sizeof(uint64_t));
	module->page_flags = kheap_alloc(page_count);
	module->page_mapped = kheap_alloc(page_count);
	sections = kheap_alloc(shnum * sizeof(*sections));
	if (module->physical_pages != NULL)
		for (size_t i = 0; i < page_count; i++)
			module->physical_pages[i] = 0;
	if (module->page_flags != NULL)
		for (size_t i = 0; i < page_count; i++)
			module->page_flags[i] = 0;
	if (module->page_mapped != NULL)
		for (size_t i = 0; i < page_count; i++)
			module->page_mapped[i] = 0;
	if (sections != NULL)
		module->sections = sections;
	if (module->physical_pages == NULL || module->page_flags == NULL ||
	    module->page_mapped == NULL || sections == NULL)
		goto no_memory;
	for (size_t i = 0; i < shnum; i++)
		sections[i] = (struct kernel_module_section_memory){0};
	for (size_t i = 0; i < shnum; i++) {
		const uint8_t *section = image + shoff + i * ELF_SECTION_SIZE;
		uint64_t flags = read64(section + 8);
		uint64_t size = read64(section + 32);
		uint64_t alignment = read64(section + 48);
		if ((flags & SHF_ALLOC) == 0 || size == 0)
			continue;
		if (alignment < PAGE_SIZE)
			alignment = PAGE_SIZE;
		/* Recompute placement from prior allocatable sections. */
		cursor = 0;
		for (size_t j = 0; j < i; j++) {
			const uint8_t *prior = image + shoff + j * ELF_SECTION_SIZE;
			uint64_t prior_flags = read64(prior + 8);
			uint64_t prior_size = read64(prior + 32);
			uint64_t prior_alignment = read64(prior + 48);
			if ((prior_flags & SHF_ALLOC) == 0 || prior_size == 0)
				continue;
			if (prior_alignment < PAGE_SIZE)
				prior_alignment = PAGE_SIZE;
			cursor = (cursor + (size_t)prior_alignment - 1U) &
			    ~((size_t)prior_alignment - 1U);
			cursor += (size_t)prior_size;
		}
		cursor = (cursor + (size_t)alignment - 1U) & ~((size_t)alignment - 1U);
		sections[i].address = (void *)(uintptr_t)(VMM_MODULE_ARENA_START +
		    slot * MODULE_SLOT_SIZE + cursor);
		sections[i].size = (size_t)size;
		for (size_t p = cursor / PAGE_SIZE;
		    p < (cursor + size + PAGE_SIZE - 1U) / PAGE_SIZE; p++) {
			if (module->physical_pages[p] == 0) {
				if (!pmm_alloc_page(&module->physical_pages[p]))
					goto no_memory;
				uint8_t *alias = (uint8_t *)(uintptr_t)(
				    vmm_hhdm_offset() + module->physical_pages[p]);
				for (size_t b = 0; b < PAGE_SIZE; b++)
					alias[b] = 0;
				if (!vmm_map_page(vmm_kernel_address_space(),
				    VMM_MODULE_ARENA_START + slot * MODULE_SLOT_SIZE +
				    p * PAGE_SIZE, module->physical_pages[p], VMM_WRITE))
					goto no_memory;
				module->page_mapped[p] = 1;
			}
			module->page_flags[p] = (flags & SHF_EXECINSTR) != 0 ? VMM_EXEC :
			    ((flags & SHF_WRITE) != 0 ? VMM_WRITE : 0);
		}
		if (read32(section + 4) != SHT_NOBITS) {
			uint64_t file_offset = read64(section + 24);
			const uint8_t *source = image + file_offset;
			uint8_t *destination = (uint8_t *)(uintptr_t)
			    (vmm_hhdm_offset() + module->physical_pages[cursor / PAGE_SIZE]) +
			    (cursor & (PAGE_SIZE - 1U));
			for (size_t b = 0; b < size; b++) {
				if ((cursor + b) / PAGE_SIZE != cursor / PAGE_SIZE)
					destination = (uint8_t *)(uintptr_t)(vmm_hhdm_offset() +
					    module->physical_pages[(cursor + b) / PAGE_SIZE]);
				destination[b % PAGE_SIZE] = source[b];
			}
		}
		if (sections[i].size > module->page_count * PAGE_SIZE - cursor)
			goto no_memory;
	}
	if (kernel_module_relocate(image, image_size, sections, shnum) !=
	    KERNEL_MODULE_OK || !find_entry_points(image, image_size, sections,
	    shnum, &init, &fini, &prepare_unload))
		goto invalid_object;
	base = VMM_MODULE_ARENA_START + slot * MODULE_SLOT_SIZE;
	for (size_t i = 0; i < page_count; i++) {
		uint64_t physical;
		if (module->physical_pages[i] == 0)
			continue;
		if (!vmm_unmap_page(vmm_kernel_address_space(), base + i * PAGE_SIZE,
		    &physical) || physical != module->physical_pages[i])
			goto invalid_object;
		module->page_mapped[i] = 0;
		if (!vmm_map_page(vmm_kernel_address_space(), base + i * PAGE_SIZE,
		    physical, module->page_flags[i]))
			goto invalid_object;
		module->page_mapped[i] = 1;
	}
	if (init() != 0)
		goto init_failed;
	module->fini = fini;
	module->prepare_unload = prepare_unload;
	lock_flags = spinlock_lock_irqsave(&module_lock);
	module->loaded = true;
	spinlock_unlock_irqrestore(&module_lock, lock_flags);
	return KERNEL_MODULE_OK;

init_failed:
	if (unmap_and_free(module, slot))
		return KERNEL_MODULE_INIT_FAILED;
	return KERNEL_MODULE_BUSY;
invalid_object:
	if (unmap_and_free(module, slot))
		return KERNEL_MODULE_UNSUPPORTED_RELOCATION;
	return KERNEL_MODULE_BUSY;
no_memory:
	if (unmap_and_free(module, slot))
		return KERNEL_MODULE_NO_MEMORY;
	return KERNEL_MODULE_BUSY;
}

enum kernel_module_error
kernel_module_unload(const char *name)
{
	struct loaded_module *module = NULL;
	size_t slot;
	uint64_t flags;
	{
		size_t name_length;
		if (!name_valid(name, &name_length))
			return KERNEL_MODULE_INVALID_ARGUMENT;
	}
	flags = spinlock_lock_irqsave(&module_lock);
	for (slot = 0; slot < MODULE_LIMIT; slot++) {
		if (modules[slot].busy && module_name_equal(&modules[slot], name)) {
			module = &modules[slot];
			break;
		}
	}
	if (module == NULL) {
		spinlock_unlock_irqrestore(&module_lock, flags);
		return KERNEL_MODULE_NOT_FOUND;
	}
	if (!module->loaded) {
		spinlock_unlock_irqrestore(&module_lock, flags);
		return KERNEL_MODULE_BUSY;
	}
	if (module->references != 0) {
		spinlock_unlock_irqrestore(&module_lock, flags);
		return KERNEL_MODULE_BUSY;
	}
	module->loaded = false;
	spinlock_unlock_irqrestore(&module_lock, flags);
	if (module->prepare_unload != NULL && module->prepare_unload() != 0) {
		flags = spinlock_lock_irqsave(&module_lock);
		module->loaded = true;
		spinlock_unlock_irqrestore(&module_lock, flags);
		return KERNEL_MODULE_BUSY;
	}
	if (module->fini != NULL)
		module->fini();
	return unmap_and_free(module, slot) ? KERNEL_MODULE_OK :
	    KERNEL_MODULE_BUSY;
}

bool
kernel_module_acquire(const char *name, struct kernel_module_handle *handle)
{
	uint64_t flags;
	size_t name_length;
	if (handle == NULL || !name_valid(name, &name_length))
		return false;
	*handle = (struct kernel_module_handle){0};
	flags = spinlock_lock_irqsave(&module_lock);
	for (size_t i = 0; i < MODULE_LIMIT; i++) {
		struct loaded_module *module = &modules[i];
		if (!module->busy || !module->loaded ||
		    !module_name_equal(module, name))
			continue;
		if (module->references == UINT32_MAX) {
			spinlock_unlock_irqrestore(&module_lock, flags);
			return false;
		}
		module->references++;
		*handle = (struct kernel_module_handle){
			.slot = (uint32_t)i,
			.generation = module->generation
		};
		spinlock_unlock_irqrestore(&module_lock, flags);
		return true;
	}
	spinlock_unlock_irqrestore(&module_lock, flags);
	return false;
}

bool
kernel_module_release(struct kernel_module_handle handle)
{
	uint64_t flags;
	if (handle.slot >= MODULE_LIMIT || handle.generation == 0)
		return false;
	flags = spinlock_lock_irqsave(&module_lock);
	{
		struct loaded_module *module = &modules[handle.slot];
		if (!module->busy || !module->loaded ||
		    module->generation != handle.generation ||
		    module->references == 0) {
			spinlock_unlock_irqrestore(&module_lock, flags);
			return false;
		}
		module->references--;
	}
	spinlock_unlock_irqrestore(&module_lock, flags);
	return true;
}

size_t
kernel_module_snapshot(struct kernel_module_snapshot_entry *entries,
	size_t capacity)
{
	uint64_t flags;
	size_t copied = 0;
	if (capacity != 0 && entries == NULL)
		return 0;
	flags = spinlock_lock_irqsave(&module_lock);
	for (size_t i = 0; i < MODULE_LIMIT && copied < capacity; i++) {
		const struct loaded_module *module = &modules[i];
		if (!module->busy || !module->loaded)
			continue;
		entries[copied] = (struct kernel_module_snapshot_entry){
			.references = module->references
		};
		for (size_t j = 0; j <= KERNEL_MODULE_NAME_MAX; j++)
			entries[copied].name[j] = module->name[j];
		copied++;
	}
	spinlock_unlock_irqrestore(&module_lock, flags);
	return copied;
}
