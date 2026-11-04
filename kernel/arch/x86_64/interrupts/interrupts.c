#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/gdt.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/user.h"
#include "kernel/log.h"
#include "kernel/process.h"
#include "kernel/smp.h"

#define IDT_ENTRIES 256
#define KERNEL_CODE_SELECTOR 0x08
#define IDT_INTERRUPT_GATE 0x8e
#define USER_SYSCALL_VECTOR 128

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
static interrupt_handler handlers[IDT_ENTRIES];
extern void *isr_stub_table[IDT_ENTRIES];

static size_t
append_text(char *buffer, size_t length, const char *text)
{
	while (*text != '\0' && length + 1U < 384U)
		buffer[length++] = *text++;
	return length;
}

static size_t
append_u64(char *buffer, size_t length, uint64_t value)
{
	char digits[20];
	size_t count = 0;

	do {
		digits[count++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0 && count < sizeof(digits));
	while (count != 0 && length + 1U < 384U)
		buffer[length++] = digits[--count];
	return length;
}

static void
idt_set_gate(unsigned int vector, uintptr_t address)
{
	idt[vector] = (struct idt_gate){
		.offset_low = address & 0xffff,
		.selector = KERNEL_CODE_SELECTOR,
		.ist = 0,
		.type_attributes = vector == USER_SYSCALL_VECTOR ? 0xee :
		    IDT_INTERRUPT_GATE,
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
idt_load(void)
{
	struct table_pointer pointer = {
		.limit = sizeof(idt) - 1,
		.base = (uint64_t)(uintptr_t)idt
	};
	__asm__ volatile ("lidt %0" : : "m" (pointer) : "memory");
}

bool
interrupt_register_handler(uint8_t vector, interrupt_handler handler)
{
	uint64_t flags;
	bool available;

	if (vector < 32 || vector == 255 || handler == NULL)
		return false;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	available = handlers[vector] == NULL;
	if (available)
		handlers[vector] = handler;
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return available;
}

bool
interrupt_register_nmi_handler(interrupt_handler handler)
{
	uint64_t flags;
	bool available;

	if (handler == NULL)
		return false;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	available = handlers[2] == NULL;
	if (available)
		handlers[2] = handler;
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return available;
}

struct interrupt_frame *
interrupt_dispatch(struct interrupt_frame *frame)
{
	struct interrupt_frame *next;

	if (frame == NULL) {
		klog("OpenSillicon panic: missing interrupt frame\n");
		kernel_halt();
	}
	if (frame->vector == 2 && handlers[2] != NULL) {
		next = handlers[2](frame);
		if (next != NULL)
			return next;
		klog("OpenSillicon panic: NMI handler returned no frame\n");
	} else if (frame->vector < 32) {
		if ((frame->cs & 3U) == 3U)
			return process_prepare_user_return(
			    process_user_exception(frame));
		{
			char diagnostic[384];
			size_t length = 0;
			const struct smp_cpu *cpu;
			uintptr_t fault_address = 0;
			uintptr_t kernel_rsp = 0;

			length = append_text(diagnostic, length,
			    "OpenSillicon exception: vector ");
			length = append_u64(diagnostic, length, frame->vector);
			length = append_text(diagnostic, length, " error ");
			length = append_u64(diagnostic, length, frame->error_code);
			length = append_text(diagnostic, length, " rip ");
			length = append_u64(diagnostic, length, frame->rip);
			length = append_text(diagnostic, length, " cs ");
			length = append_u64(diagnostic, length, frame->cs);
			length = append_text(diagnostic, length, " rflags ");
			length = append_u64(diagnostic, length, frame->rflags);
			length = append_text(diagnostic, length, " frame ");
			length = append_u64(diagnostic, length, (uintptr_t)frame);
			if ((frame->cs & 3U) == 0) {
				kernel_rsp = frame->user_rsp;
				length = append_text(diagnostic, length, " kernel_rsp ");
				length = append_u64(diagnostic, length, kernel_rsp);
			}
			length = append_text(diagnostic, length, " pid ");
			length = append_u64(diagnostic, length,
			    process_current_pid());
			cpu = smp_current_cpu();
			if (cpu != NULL) {
				length = append_text(diagnostic, length, " cpu ");
				length = append_u64(diagnostic, length, cpu->logical_id);
			}
			if (frame->vector == 14) {
				__asm__ volatile ("mov %%cr2, %0" : "=r" (fault_address));
				length = append_text(diagnostic, length, " cr2 ");
				length = append_u64(diagnostic, length,
				    fault_address);
			}
			length = append_text(diagnostic, length, "\n");
			diagnostic[length] = '\0';
			klog(diagnostic);
		}
	} else if (frame->vector == 255) {
		return process_prepare_user_return(frame);
	} else if (frame->vector < IDT_ENTRIES && handlers[frame->vector] != NULL) {
		next = handlers[frame->vector](frame);

		if (next != NULL)
			return process_prepare_user_return(next);
		klog("OpenSillicon panic: interrupt handler returned no frame\n");
	} else {
		klog("OpenSillicon panic: unexpected interrupt vector ");
		klog_u64(frame->vector);
		klog("\n");
	}
	kernel_halt();
}
