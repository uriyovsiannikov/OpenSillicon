#ifndef ARCH_X86_64_INTERRUPTS_H
#define ARCH_X86_64_INTERRUPTS_H

#include <stdbool.h>
#include <stdint.h>

struct interrupt_frame {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
	uint64_t vector;
	uint64_t error_code;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t user_rsp;
	uint64_t user_ss;
};

typedef struct interrupt_frame *(*interrupt_handler)(
	struct interrupt_frame *frame);

void idt_init(void);
void idt_load(void);
bool interrupt_register_handler(uint8_t vector, interrupt_handler handler);
struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame);

#endif
