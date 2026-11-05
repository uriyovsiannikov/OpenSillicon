#ifndef ARCH_X86_64_USER_H
#define ARCH_X86_64_USER_H

#include <stdint.h>

uint64_t user_enter(uint64_t entry, uint64_t stack_pointer);
_Noreturn void user_return_to_kernel(uint64_t status);

struct user_kernel_return {
	uint64_t stack_pointer;
	uint64_t status;
};
/* Assembly entry helpers: one outstanding user entry per logical CPU. */
void arch_user_context_save(uint64_t stack_pointer);
struct user_kernel_return arch_user_context_return(uint64_t status);

#endif
