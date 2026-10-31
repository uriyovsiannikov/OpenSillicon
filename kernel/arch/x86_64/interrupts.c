#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/interrupts.h"
#include "kernel/log.h"

#define IDT_ENTRIES 256
#define KERNEL_CODE_SELECTOR 0x08
#define IDT_INTERRUPT_GATE 0x8e

struct idt_gate {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type_attributes;
	uint16_t offset_middle;
	uint32_t offset_high;
	uint32_t reserved;
} __attribute__((packed));

struct table_pointer {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

static struct idt_gate idt[IDT_ENTRIES];
extern void *isr_stub_table[IDT_ENTRIES];

static void
idt_set_gate(unsigned int vector, uintptr_t address)
{
	idt[vector] = (struct idt_gate){
		.offset_low = address & 0xffff,
		.selector = KERNEL_CODE_SELECTOR,
		.ist = 0,
		.type_attributes = IDT_INTERRUPT_GATE,
		.offset_middle = (address >> 16) & 0xffff,
		.offset_high = address >> 32,
		.reserved = 0
	};
}

void
idt_init(void)
{
	struct table_pointer pointer = {
		.limit = sizeof(idt) - 1,
		.base = (uint64_t)(uintptr_t)idt
	};
	unsigned int vector;

	__asm__ volatile ("cli");
	gdt_init();
	for (vector = 0; vector < IDT_ENTRIES; vector++)
		idt_set_gate(vector, (uintptr_t)isr_stub_table[vector]);
	__asm__ volatile ("lidt %0" : : "m" (pointer));
}

void
interrupt_dispatch(struct interrupt_frame *frame)
{
	if (frame == NULL) {
		klog("OpenSillicon panic: missing interrupt frame\n");
		kernel_halt();
	}
	if (frame->vector < 32) {
		klog("OpenSillicon exception: vector ");
		klog_u64(frame->vector);
		klog(" error ");
		klog_u64(frame->error_code);
		klog(" rip ");
		klog_u64(frame->rip);
		if (frame->vector == 14) {
			uintptr_t fault_address;

			__asm__ volatile ("mov %%cr2, %0" : "=r" (fault_address));
			klog(" cr2 ");
			klog_u64(fault_address);
		}
		klog("\n");
	} else {
		klog("OpenSillicon panic: unexpected interrupt vector ");
		klog_u64(frame->vector);
		klog("\n");
	}
	kernel_halt();
}
