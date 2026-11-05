#ifndef KERNEL_BLOCK_H
#define KERNEL_BLOCK_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define BLOCK_DEVICE_NAME_MAX 31U
#define BLOCK_DEVICE_MAX 8U

struct block_device;

struct block_device_operations {
	int (*read)(struct block_device *device, uint64_t first_sector,
	    uint32_t sector_count, void *buffer);
	int (*write)(struct block_device *device, uint64_t first_sector,
	    uint32_t sector_count, const void *buffer);
	int (*flush)(struct block_device *device);
};

struct block_device {
	char name[BLOCK_DEVICE_NAME_MAX + 1];
	uint32_t logical_sector_size;
	uint64_t sector_count;
	const struct block_device_operations *operations;
	void *context;
	bool registered;
};

int block_device_register(struct block_device *device);
size_t block_device_count(void);
struct block_device *block_device_at(size_t index);
int block_read(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, void *buffer);
int block_write(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, const void *buffer);
int block_flush(struct block_device *device);

#endif
