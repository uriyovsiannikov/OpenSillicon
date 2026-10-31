#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/gdt.h"

#define GDT_ENTRIES 7
#define KERNEL_CODE_SELECTOR 0x08
#define TSS_SELECTOR 0x28
#define KERNEL_STACK_SIZE 16384

struct tss64 {
	uint32_t reserved0;
	uint64_t rsp0;
	uint64_t rsp1;
	uint64_t rsp2;
	uint64_t reserved1;
	uint64_t ist1;
	uint64_t ist2;
	uint64_t ist3;
	uint64_t ist4;
	uint64_t ist5;
	uint64_t ist6;
	uint64_t ist7;
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t iomap_base;
} __attribute__((packed));

struct table_pointer {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

static uint64_t gdt[GDT_ENTRIES];
static struct tss64 tss;
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
extern void gdt_load(const struct table_pointer *pointer);

static void
set_tss_descriptor(unsigned int index, uint64_t base, uint32_t limit)
{
	gdt[index] = (limit & 0xffff) |
	    ((base & 0xffffff) << 16) |
	    (UINT64_C(0x89) << 40) |
	    ((uint64_t)(limit & 0xf0000) << 32) |
	    ((base & UINT64_C(0xff000000)) << 32);
	gdt[index + 1] = base >> 32;
}

void
gdt_init(void)
{
	struct table_pointer pointer = {
		.limit = sizeof(gdt) - 1,
		.base = (uint64_t)(uintptr_t)gdt
	};

	gdt[0] = 0;
	gdt[1] = UINT64_C(0x00af9a000000ffff);
	gdt[2] = UINT64_C(0x00cf92000000ffff);
	gdt[3] = UINT64_C(0x00cff2000000ffff);
	gdt[4] = UINT64_C(0x00affa000000ffff);
	tss = (struct tss64){0};
	tss.rsp0 = (uint64_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack));
	tss.iomap_base = sizeof(tss);
	set_tss_descriptor(5, (uint64_t)(uintptr_t)&tss, sizeof(tss) - 1);
	gdt_load(&pointer);
	__asm__ volatile ("ltr %0" : : "r" ((uint16_t)TSS_SELECTOR));
}

void
gdt_set_kernel_stack(uint64_t stack_top)
{
	tss.rsp0 = stack_top;
}

void
gdt_restore_kernel_stack(void)
{
	tss.rsp0 = (uint64_t)(uintptr_t)(kernel_stack + sizeof(kernel_stack));
}
