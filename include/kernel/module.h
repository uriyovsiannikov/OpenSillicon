#ifndef KERNEL_MODULE_H
#define KERNEL_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_MODULE_NAME_MAX 63U
#define KERNEL_MODULE_LIMIT 32U
#define KERNEL_MODULE_MAX_SECTIONS 256U
#define KERNEL_MODULE_SYMBOL_MAX 128U
#define KERNEL_MODULE_SYMBOL_NAME_MAX 127U

enum kernel_module_error {
	KERNEL_MODULE_OK = 0,
	KERNEL_MODULE_INVALID_ARGUMENT,
	KERNEL_MODULE_INVALID_ELF,
	KERNEL_MODULE_UNSUPPORTED,
	KERNEL_MODULE_MALFORMED_SECTION,
	KERNEL_MODULE_MALFORMED_SYMBOL,
	KERNEL_MODULE_UNSUPPORTED_RELOCATION,
	KERNEL_MODULE_NO_MEMORY,
	KERNEL_MODULE_ALREADY_LOADED,
	KERNEL_MODULE_NOT_FOUND,
	KERNEL_MODULE_INIT_FAILED,
	KERNEL_MODULE_BUSY,
	KERNEL_MODULE_IO_ERROR
};

struct kernel_module_info {
	char name[KERNEL_MODULE_NAME_MAX + 1U];
	size_t allocatable_sections;
	size_t relocation_sections;
	size_t relocations;
	size_t image_size;
};

struct kernel_module_section_memory {
	void *address;
	size_t size;
};

struct kernel_module_handle {
	uint32_t slot;
	uint32_t generation;
};

struct kernel_module_snapshot_entry {
	char name[KERNEL_MODULE_NAME_MAX + 1U];
	uint32_t references;
	uint32_t reserved;
};

/* Validates the bounded x86-64 ELF64 ET_REL subset accepted for kernel
 * modules. This does not allocate, relocate, or execute the object. */
enum kernel_module_error kernel_module_validate(const void *image,
	size_t image_size, struct kernel_module_info *info);
/* Adds an explicitly exported kernel symbol. Names are immutable and duplicate
 * exports are rejected. */
bool kernel_module_export_symbol(const char *name, const void *address);
/* Applies supported ELF64 RELA entries to caller-placed alloc sections. The
 * section array is indexed by ELF section number and points at writable
 * staging memory. All entries are resolved and range-checked before writes. */
enum kernel_module_error kernel_module_relocate(const void *image,
	size_t image_size, struct kernel_module_section_memory *sections,
	size_t section_count);
/* Loads a named module from an in-memory ELF object and calls module_init().
 * The image must remain readable only for the duration of this call. */
enum kernel_module_error kernel_module_load(const char *name,
	const void *image, size_t image_size);
/* Load a regular module file through the kernel VFS after root is mounted. */
enum kernel_module_error kernel_module_load_path(const char *name,
	const char *path);
/* Calls optional module_prepare_unload() first. A nonzero return keeps the
 * module loaded and skips module_fini and unmapping. Preparation must leave
 * the module usable if it refuses. */
enum kernel_module_error kernel_module_unload(const char *name);
/* Pins module text/data for a client that retains callback pointers. Each
 * successful acquire must be balanced with exactly one release. */
bool kernel_module_acquire(const char *name, struct kernel_module_handle *handle);
bool kernel_module_release(struct kernel_module_handle handle);
size_t kernel_module_snapshot(struct kernel_module_snapshot_entry *entries,
	size_t capacity);

#endif
