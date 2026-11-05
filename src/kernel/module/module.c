#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/module.h"
#include "kernel/spinlock.h"

#define ELF_HEADER_SIZE 64U
#define ELF_SECTION_SIZE 64U
#define ELF_SYMBOL_SIZE 24U
#define ELF_RELA_SIZE 24U
#define ELF_ET_REL 1U
#define ELF_EM_X86_64 62U
#define SHT_NULL 0U
#define SHT_PROGBITS 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define SHT_RELA 4U
#define SHT_NOBITS 8U
#define SHT_REL 9U
#define SHF_ALLOC 0x2U
#define SHF_EXECINSTR 0x4U
#define SHF_TLS 0x400U
#define SHF_WRITE 0x1U
#define SHN_UNDEF 0U
#define SHN_XINDEX 0xffffU
#define R_X86_64_NONE 0U
#define R_X86_64_64 1U
#define R_X86_64_PC32 2U
#define R_X86_64_PLT32 4U
#define R_X86_64_32 10U
#define R_X86_64_32S 11U

struct exported_symbol {
	bool occupied;
	char name[KERNEL_MODULE_SYMBOL_NAME_MAX + 1U];
	uintptr_t address;
};

static struct exported_symbol exports[KERNEL_MODULE_SYMBOL_MAX];
static struct spinlock export_lock = SPINLOCK_INITIALIZER;

struct elf_section {
	uint32_t name;
	uint32_t type;
	uint64_t flags;
	uint64_t offset;
	uint64_t size;
	uint32_t link;
	uint32_t info;
	uint64_t alignment;
	uint64_t entry_size;
};

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

static void
write32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (i * 8U));
}

static void
write64(uint8_t *p, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (i * 8U));
}

static bool
range_valid(uint64_t offset, uint64_t length, size_t limit)
{
	return offset <= limit && length <= (uint64_t)limit - offset;
}

static bool
power_of_two(uint64_t value)
{
	return value == 0 || (value & (value - 1U)) == 0;
}

static bool
string_valid(const uint8_t *strings, size_t length, uint32_t offset)
{
	if (offset >= length)
		return false;
	for (size_t i = offset; i < length; i++)
		if (strings[i] == 0)
			return true;
	return false;
}

static bool
relocation_supported(uint32_t type)
{
	return type == R_X86_64_NONE || type == R_X86_64_64 ||
	    type == R_X86_64_PC32 || type == R_X86_64_PLT32 ||
	    type == R_X86_64_32 || type == R_X86_64_32S;
}

static size_t
bounded_name_length(const char *name, size_t limit)
{
	size_t length = 0;
	if (name == NULL)
		return 0;
	while (length < limit && name[length] != '\0')
		length++;
	return length;
}

bool
kernel_module_export_symbol(const char *name, const void *address)
{
	size_t length = bounded_name_length(name,
	    KERNEL_MODULE_SYMBOL_NAME_MAX + 1U);
	uint64_t flags;
	size_t free_slot = KERNEL_MODULE_SYMBOL_MAX;
	if (length == 0 || length > KERNEL_MODULE_SYMBOL_NAME_MAX ||
	    address == NULL)
		return false;
	flags = spinlock_lock_irqsave(&export_lock);
	for (size_t i = 0; i < KERNEL_MODULE_SYMBOL_MAX; i++) {
		if (!exports[i].occupied) {
			if (free_slot == KERNEL_MODULE_SYMBOL_MAX)
				free_slot = i;
			continue;
		}
		if (bounded_name_length(exports[i].name,
		    sizeof(exports[i].name)) == length) {
			bool same = true;
			for (size_t j = 0; j < length; j++)
				if (exports[i].name[j] != name[j])
					same = false;
			if (same) {
				spinlock_unlock_irqrestore(&export_lock, flags);
				return false;
			}
		}
	}
	if (free_slot == KERNEL_MODULE_SYMBOL_MAX) {
		spinlock_unlock_irqrestore(&export_lock, flags);
		return false;
	}
	for (size_t i = 0; i < length; i++)
		exports[free_slot].name[i] = name[i];
	exports[free_slot].name[length] = '\0';
	exports[free_slot].address = (uintptr_t)address;
	exports[free_slot].occupied = true;
	spinlock_unlock_irqrestore(&export_lock, flags);
	return true;
}

static bool
resolve_export(const uint8_t *strings, size_t string_size, uint32_t name_offset,
	uintptr_t *address)
{
	size_t length;
	uint64_t flags;
	if (!string_valid(strings, string_size, name_offset))
		return false;
	length = bounded_name_length((const char *)strings + name_offset,
	    string_size - name_offset);
	if (length == 0 || length > KERNEL_MODULE_SYMBOL_NAME_MAX)
		return false;
	flags = spinlock_lock_irqsave(&export_lock);
	for (size_t i = 0; i < KERNEL_MODULE_SYMBOL_MAX; i++) {
		bool same = exports[i].occupied &&
		    bounded_name_length(exports[i].name,
		    sizeof(exports[i].name)) == length;
		for (size_t j = 0; same && j < length; j++)
			if (exports[i].name[j] != strings[name_offset + j])
				same = false;
		if (same) {
			*address = exports[i].address;
			spinlock_unlock_irqrestore(&export_lock, flags);
			return true;
		}
	}
	spinlock_unlock_irqrestore(&export_lock, flags);
	return false;
}

static bool
relocation_value(const uint8_t *symbol, const uint8_t *strings,
	size_t strings_size, const struct kernel_module_section_memory *runtime_sections,
	size_t runtime_section_count, uint32_t type, uint64_t *value)
{
	uint16_t section_index = read16(symbol + 6);
	uint32_t name_offset = read32(symbol);
	uint64_t symbol_value = read64(symbol + 8);
	uintptr_t resolved;
	if (section_index == SHN_UNDEF) {
		uint8_t binding = symbol[4] >> 4;
		if (name_offset == 0)
			resolved = 0;
		else if (!resolve_export(strings, strings_size, name_offset,
		    &resolved)) {
			if (binding != 2U)
				return false;
			resolved = 0;
		}
	} else if (section_index == 0xfff1U) {
		resolved = (uintptr_t)symbol_value;
	} else {
		if (section_index >= runtime_section_count ||
		    section_index >= KERNEL_MODULE_MAX_SECTIONS ||
		    runtime_sections[section_index].address == NULL ||
		    symbol_value > runtime_sections[section_index].size ||
		    symbol_value > UINTPTR_MAX -
		    (uintptr_t)runtime_sections[section_index].address)
			return false;
		resolved = (uintptr_t)runtime_sections[section_index].address +
		    (uintptr_t)symbol_value;
	}
	if (type == R_X86_64_NONE) {
		*value = 0;
		return true;
	}
	/* The caller adds the signed RELA addend and applies the relocation's
	 * width/range rules, so return the resolved symbol address only. */
	*value = (uint64_t)resolved;
	return true;
}

static bool
relocation_result(uint32_t type, uint64_t symbol, int64_t addend,
	uintptr_t place, uint64_t *result)
{
	__int128 value = (__int128)symbol + addend;
	if (type == R_X86_64_64) {
		if (value < 0 || value > UINT64_MAX)
			return false;
		*result = (uint64_t)value;
		return true;
	}
	if (type == R_X86_64_PC32 || type == R_X86_64_PLT32)
		value -= place;
	if (type == R_X86_64_32) {
		if (value < 0 || value > UINT32_MAX)
			return false;
	} else if (type == R_X86_64_32S) {
		/* A sign-extended 32-bit kernel address is represented as a large
		 * uint64_t, not as a positive signed displacement. */
		if (value < INT64_MIN || value > UINT64_MAX ||
		    (uint64_t)(int64_t)(int32_t)(uint32_t)value != (uint64_t)value)
			return false;
	} else if (type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
		if (value < INT32_MIN || value > INT32_MAX)
			return false;
	} else {
		return type == R_X86_64_NONE;
	}
	*result = (uint32_t)value;
	return true;
}

enum kernel_module_error
kernel_module_relocate(const void *image, size_t image_size,
	struct kernel_module_section_memory *runtime_sections,
	size_t runtime_section_count)
{
	const uint8_t *bytes = image;
	struct elf_section sections[KERNEL_MODULE_MAX_SECTIONS];
	struct kernel_module_info info;
	uint64_t section_offset;
	uint16_t section_count;
	enum kernel_module_error validation;
	if (bytes == NULL || runtime_sections == NULL)
		return KERNEL_MODULE_INVALID_ARGUMENT;
	validation = kernel_module_validate(image, image_size, &info);
	if (validation != KERNEL_MODULE_OK)
		return validation;
	section_offset = read64(bytes + 40);
	section_count = read16(bytes + 60);
	if (runtime_section_count < section_count)
		return KERNEL_MODULE_INVALID_ARGUMENT;
	for (size_t i = 0; i < section_count; i++) {
		const uint8_t *raw = bytes + section_offset + i * ELF_SECTION_SIZE;
		sections[i] = (struct elf_section){
			.name = read32(raw), .type = read32(raw + 4),
			.flags = read64(raw + 8), .offset = read64(raw + 24),
			.size = read64(raw + 32), .link = read32(raw + 40),
			.info = read32(raw + 44), .alignment = read64(raw + 48),
			.entry_size = read64(raw + 56)
		};
		if ((sections[i].flags & SHF_ALLOC) != 0 &&
		    sections[i].size != 0 &&
		    (runtime_sections[i].address == NULL ||
		    runtime_sections[i].size < sections[i].size))
			return KERNEL_MODULE_INVALID_ARGUMENT;
	}
	/* First pass resolves every reference and checks every computed value. */
	for (size_t i = 0; i < section_count; i++) {
		const struct elf_section *rela_section = &sections[i];
		const struct elf_section *symbol_table;
		const struct elf_section *string_table;
		if (rela_section->type != SHT_RELA)
			continue;
		symbol_table = &sections[rela_section->link];
		string_table = &sections[symbol_table->link];
		for (size_t j = 0; j < rela_section->size / ELF_RELA_SIZE; j++) {
			const uint8_t *rela = bytes + rela_section->offset +
			    j * ELF_RELA_SIZE;
			uint64_t relocation_info = read64(rela + 8);
			uint32_t symbol_index = (uint32_t)(relocation_info >> 32);
			uint32_t type = (uint32_t)relocation_info;
			const uint8_t *symbol = bytes + symbol_table->offset +
			    (uint64_t)symbol_index * ELF_SYMBOL_SIZE;
			uint64_t symbol_address;
			uint64_t place_offset = read64(rela);
			uint64_t value;
			if (type == R_X86_64_NONE)
				continue;
			if (place_offset > UINTPTR_MAX -
			    (uintptr_t)runtime_sections[rela_section->info].address ||
			    !relocation_value(symbol,
			    bytes + string_table->offset, string_table->size,
			    runtime_sections, runtime_section_count, type,
			    &symbol_address) ||
			    !relocation_result(type, symbol_address,
			    (int64_t)read64(rela + 16),
			    (uintptr_t)runtime_sections[rela_section->info].address +
			    (uintptr_t)place_offset, &value))
				return KERNEL_MODULE_UNSUPPORTED_RELOCATION;
		}
	}
	/* Second pass writes only after complete preflight succeeded. */
	for (size_t i = 0; i < section_count; i++) {
		const struct elf_section *rela_section = &sections[i];
		const struct elf_section *symbol_table;
		const struct elf_section *string_table;
		if (rela_section->type != SHT_RELA)
			continue;
		symbol_table = &sections[rela_section->link];
		string_table = &sections[symbol_table->link];
		for (size_t j = 0; j < rela_section->size / ELF_RELA_SIZE; j++) {
			const uint8_t *rela = bytes + rela_section->offset +
			    j * ELF_RELA_SIZE;
			uint64_t relocation_info = read64(rela + 8);
			uint32_t symbol_index = (uint32_t)(relocation_info >> 32);
			uint32_t type = (uint32_t)relocation_info;
			const uint8_t *symbol = bytes + symbol_table->offset +
			    (uint64_t)symbol_index * ELF_SYMBOL_SIZE;
			uint64_t symbol_address;
			uint64_t place_offset = read64(rela);
			uint64_t value;
			uint8_t *place = (uint8_t *)runtime_sections[
			    rela_section->info].address + place_offset;
			if (type == R_X86_64_NONE)
				continue;
		(void)relocation_value(symbol, bytes + string_table->offset,
			    string_table->size, runtime_sections, runtime_section_count,
			    type, &symbol_address);
		(void)relocation_result(type, symbol_address,
		    (int64_t)read64(rela + 16), (uintptr_t)place, &value);
		if (type == R_X86_64_64)
			write64(place, value);
		else
			write32(place, (uint32_t)value);
		}
	}
	return KERNEL_MODULE_OK;
}

enum kernel_module_error
kernel_module_validate(const void *image, size_t image_size,
	struct kernel_module_info *info)
{
	const uint8_t *bytes = image;
	struct elf_section sections[KERNEL_MODULE_MAX_SECTIONS];
	uint64_t section_offset;
	uint16_t section_count;
	uint16_t string_index;
	const uint8_t *section_strings;
	size_t section_strings_length;
	struct kernel_module_info candidate = {0};
	if (info != NULL)
		*info = candidate;
	if (bytes == NULL || info == NULL)
		return KERNEL_MODULE_INVALID_ARGUMENT;
	if (image_size < ELF_HEADER_SIZE || bytes[0] != 0x7f ||
	    bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F' ||
	    bytes[4] != 2 || bytes[5] != 1 || bytes[6] != 1 ||
	    read16(bytes + 16) != ELF_ET_REL ||
	    read16(bytes + 18) != ELF_EM_X86_64 || read32(bytes + 20) != 1 ||
	    read32(bytes + 48) != 0 ||
	    read16(bytes + 52) != ELF_HEADER_SIZE ||
	    read16(bytes + 58) != ELF_SECTION_SIZE)
		return KERNEL_MODULE_INVALID_ELF;
	section_offset = read64(bytes + 40);
	section_count = read16(bytes + 60);
	string_index = read16(bytes + 62);
	if (section_count == 0 || section_count > KERNEL_MODULE_MAX_SECTIONS ||
	    string_index == SHN_XINDEX || string_index >= section_count ||
	    !range_valid(section_offset,
	    (uint64_t)section_count * ELF_SECTION_SIZE, image_size))
		return KERNEL_MODULE_INVALID_ELF;
	for (size_t i = 0; i < section_count; i++) {
		const uint8_t *raw = bytes + section_offset + i * ELF_SECTION_SIZE;
		struct elf_section *section = &sections[i];
		section->name = read32(raw);
		section->type = read32(raw + 4);
		section->flags = read64(raw + 8);
		section->offset = read64(raw + 24);
		section->size = read64(raw + 32);
		section->link = read32(raw + 40);
		section->info = read32(raw + 44);
		section->alignment = read64(raw + 48);
		section->entry_size = read64(raw + 56);
		if (!power_of_two(section->alignment) ||
		    section->offset > image_size ||
		    (section->type != SHT_NOBITS &&
		    !range_valid(section->offset, section->size, image_size)))
			return KERNEL_MODULE_MALFORMED_SECTION;
		if ((section->flags & SHF_TLS) != 0)
			return KERNEL_MODULE_UNSUPPORTED;
		if ((section->flags & SHF_ALLOC) != 0) {
			if (section->type != SHT_PROGBITS &&
			    section->type != SHT_NOBITS)
				return KERNEL_MODULE_UNSUPPORTED;
			if ((section->flags & (SHF_WRITE | SHF_EXECINSTR)) ==
			    (SHF_WRITE | SHF_EXECINSTR))
				return KERNEL_MODULE_UNSUPPORTED;
			candidate.allocatable_sections++;
		}
	}
	if (sections[0].type != SHT_NULL ||
	    sections[string_index].type != SHT_STRTAB)
		return KERNEL_MODULE_MALFORMED_SECTION;
	section_strings = bytes + sections[string_index].offset;
	section_strings_length = sections[string_index].size;
	for (size_t i = 1; i < section_count; i++) {
		const struct elf_section *section = &sections[i];
		if (!string_valid(section_strings, section_strings_length,
		    section->name))
			return KERNEL_MODULE_MALFORMED_SECTION;
		if (section->type == SHT_REL)
			return KERNEL_MODULE_UNSUPPORTED;
		if (section->type == SHT_SYMTAB) {
			const struct elf_section *strings;
			if (section->entry_size != ELF_SYMBOL_SIZE ||
			    section->size % ELF_SYMBOL_SIZE != 0 ||
			    section->link >= section_count ||
			    sections[section->link].type != SHT_STRTAB)
				return KERNEL_MODULE_MALFORMED_SYMBOL;
			strings = &sections[section->link];
			for (size_t j = 0; j < section->size / ELF_SYMBOL_SIZE; j++) {
				const uint8_t *symbol = bytes + section->offset +
				    j * ELF_SYMBOL_SIZE;
				uint32_t name = read32(symbol);
				uint16_t index = read16(symbol + 6);
				if ((name != 0 && !string_valid(bytes + strings->offset,
				    strings->size, name)) ||
				    (index != SHN_UNDEF && index != 0xfff1U &&
				    index >= section_count))
					return KERNEL_MODULE_MALFORMED_SYMBOL;
			}
		}
		if (section->type == SHT_RELA) {
			const struct elf_section *symbols;
			const struct elf_section *target;
			if (section->entry_size != ELF_RELA_SIZE ||
			    section->size % ELF_RELA_SIZE != 0 ||
			    section->link >= section_count ||
			    sections[section->link].type != SHT_SYMTAB ||
			    section->info >= section_count)
				return KERNEL_MODULE_MALFORMED_SECTION;
			symbols = &sections[section->link];
			target = &sections[section->info];
			if ((target->flags & SHF_ALLOC) == 0)
				return KERNEL_MODULE_MALFORMED_SECTION;
			candidate.relocation_sections++;
			for (size_t j = 0; j < section->size / ELF_RELA_SIZE; j++) {
				const uint8_t *rela = bytes + section->offset +
				    j * ELF_RELA_SIZE;
				uint64_t info_field = read64(rela + 8);
				uint32_t symbol_index = (uint32_t)(info_field >> 32);
				uint32_t type = (uint32_t)info_field;
				uint64_t offset = read64(rela);
				size_t width = type == R_X86_64_64 ? sizeof(uint64_t) :
				    sizeof(uint32_t);
				if (symbol_index >= symbols->size / ELF_SYMBOL_SIZE ||
				    !relocation_supported(type))
					return KERNEL_MODULE_UNSUPPORTED_RELOCATION;
				if (type != R_X86_64_NONE &&
				    (offset > target->size ||
				    target->size - offset < width))
					return KERNEL_MODULE_MALFORMED_SECTION;
				candidate.relocations++;
			}
		}
	}
	candidate.image_size = image_size;
	for (size_t i = 0; i < KERNEL_MODULE_NAME_MAX; i++)
		candidate.name[i] = 0;
	*info = candidate;
	return KERNEL_MODULE_OK;
}
