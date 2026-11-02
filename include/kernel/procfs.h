#ifndef KERNEL_PROCFS_H
#define KERNEL_PROCFS_H

#include "kernel/vfs.h"

int procfs_mount(const char *path);

#endif
