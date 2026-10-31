#ifndef ARCH_X86_64_USER_H
#define ARCH_X86_64_USER_H

#include <stdint.h>

uint64_t user_enter(uint64_t entry, uint64_t stack_pointer);
_Noreturn void user_return_to_kernel(uint64_t status);
extern const unsigned char _binary_user_test_bin_start[];
extern const unsigned char _binary_user_test_bin_end[];
extern const unsigned char _binary_elf_test_bin_start[];
extern const unsigned char _binary_elf_test_bin_end[];
extern const unsigned char _binary_vfs_test_bin_start[];
extern const unsigned char _binary_vfs_test_bin_end[];

#endif
