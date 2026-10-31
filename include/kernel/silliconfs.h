#ifndef KERNEL_SILLICONFS_H
#define KERNEL_SILLICONFS_H

#include <stdint.h>

#include "kernel/vfs.h"

#define SILLICONFS_BLOCK_SIZE 4096U
#define SILLICONFS_MAX_FILE_SIZE (1036ULL * SILLICONFS_BLOCK_SIZE)

struct silliconfs_device {
	void *context;
	uint32_t block_count;
	int (*read_block)(void *context, uint32_t block_number, void *block);
	int (*write_block)(void *context, uint32_t block_number,
	    const void *block);
};

/* Returns zero and a referenced root vnode on success, negative errno on error. */
int silliconfs_mount(const struct silliconfs_device *device,
	struct vnode **root);

#endif
