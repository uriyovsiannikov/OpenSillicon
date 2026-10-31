#ifndef ARCH_X86_64_GDT_H
#define ARCH_X86_64_GDT_H

#include <stdint.h>

void gdt_init(void);
void gdt_set_kernel_stack(uint64_t stack_top);
void gdt_restore_kernel_stack(void);

#endif
