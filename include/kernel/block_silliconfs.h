#ifndef KERNEL_BLOCK_SILLICONFS_H
#define KERNEL_BLOCK_SILLICONFS_H

#include "kernel/block.h"
#include "kernel/silliconfs.h"

struct block_silliconfs_adapter {
	struct block_device *block;
	struct silliconfs_device filesystem;
	_Alignas(4096) uint8_t bounce[4096];
};

bool block_silliconfs_init(struct block_silliconfs_adapter *adapter,
	struct block_device *device);

#endif
