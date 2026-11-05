#ifndef ARCH_X86_64_FPU_H
#define ARCH_X86_64_FPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARCH_FPU_STATE_SIZE 512U

bool arch_fpu_init(void);
void arch_fpu_save(void *state);
void arch_fpu_restore(const void *state);
void arch_fpu_init_process_state(void *state);
void arch_fpu_restore_kernel_state(void);

#endif
