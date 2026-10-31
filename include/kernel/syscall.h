#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include "kernel/fd.h"

#define SYSCALL_GET_ABI_VERSION 0U
#define SYSCALL_EXIT 1U
#define SYSCALL_WRITE 2U
#define SYSCALL_GETPID 3U
#define SYSCALL_FORK 4U
#define SYSCALL_EXEC 5U
#define SYSCALL_WAITPID 6U
#define SYSCALL_ABI_VERSION 1U

void syscall_init(struct fd_table *table);

#endif
