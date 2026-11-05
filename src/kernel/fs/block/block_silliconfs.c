#include "kernel/block_silliconfs.h"
#include "kernel/errno.h"

static int
read_block(void *context, uint32_t block_number, void *buffer)
{
	struct block_silliconfs_adapter *adapter = context;
	uint64_t first_sector = (uint64_t)block_number *
	    (4096U / adapter->block->logical_sector_size);
	uint32_t count = 4096U / adapter->block->logical_sector_size;
	int error = block_read(adapter->block, first_sector, count,
	    adapter->bounce);

	if (error == 0)
		for (size_t i = 0; i < sizeof(adapter->bounce); i++)
			((uint8_t *)buffer)[i] = adapter->bounce[i];
	return error;
}

static int
write_block(void *context, uint32_t block_number, const void *buffer)
{
	struct block_silliconfs_adapter *adapter = context;
	uint64_t first_sector = (uint64_t)block_number *
	    (4096U / adapter->block->logical_sector_size);
	uint32_t count = 4096U / adapter->block->logical_sector_size;
	int error;

	for (size_t i = 0; i < sizeof(adapter->bounce); i++)
		adapter->bounce[i] = ((const uint8_t *)buffer)[i];
	error = block_write(adapter->block, first_sector, count, adapter->bounce);
	return error;
}

bool
block_silliconfs_init(struct block_silliconfs_adapter *adapter,
	struct block_device *device)
{
	uint64_t total_bytes;
	uint64_t block_count;

	if (adapter == NULL || device == NULL || !device->registered ||
	    (device->logical_sector_size != 512 &&
	    device->logical_sector_size != 4096) ||
	    device->sector_count > UINT64_MAX / device->logical_sector_size)
		return false;
	total_bytes = device->sector_count * device->logical_sector_size;
	block_count = total_bytes / 4096;
	if (block_count == 0 || block_count > UINT32_MAX)
		return false;
	adapter->block = device;
	adapter->filesystem = (struct silliconfs_device){
		.context = adapter,
		.block_count = (uint32_t)block_count,
		.read_block = read_block,
		.write_block = write_block
	};
	return true;
}
