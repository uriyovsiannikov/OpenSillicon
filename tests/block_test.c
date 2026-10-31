#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/block.h"
#include "kernel/block_silliconfs.h"

struct fake_disk {
	_Alignas(4096) uint8_t bytes[64 * 4096];
	uint32_t sector_size;
	int failure;
	uint64_t last_lba;
	uint32_t last_count;
};

static int
fake_read(struct block_device *device, uint64_t lba, uint32_t count,
	void *buffer)
{
	struct fake_disk *disk = device->context;
	disk->last_lba = lba;
	disk->last_count = count;
	if (disk->failure)
		return -EIO;
	memcpy(buffer, disk->bytes + lba * disk->sector_size,
	    (size_t)count * disk->sector_size);
	return 0;
}

static int
fake_write(struct block_device *device, uint64_t lba, uint32_t count,
	const void *buffer)
{
	struct fake_disk *disk = device->context;
	disk->last_lba = lba;
	disk->last_count = count;
	if (disk->failure)
		return -EIO;
	memcpy(disk->bytes + lba * disk->sector_size, buffer,
	    (size_t)count * disk->sector_size);
	return 0;
}

static int
fake_flush(struct block_device *device)
{
	struct fake_disk *disk = device->context;
	return disk->failure ? -EIO : 0;
}

static const struct block_device_operations operations = {
	.read = fake_read,
	.write = fake_write,
	.flush = fake_flush
};

static const struct block_device_operations no_flush_operations = {
	.read = fake_read,
	.write = fake_write
};

static struct fake_disk disk;
static struct block_device block;

static void
test_sector_size(uint32_t sector_size)
{
	struct block_silliconfs_adapter adapter;
	_Alignas(4096) uint8_t buffer[4096];

	disk.sector_size = sector_size;
	block.logical_sector_size = sector_size;
	block.sector_count = sizeof(disk.bytes) / sector_size;
	assert(block_silliconfs_init(&adapter, &block));
	assert(adapter.filesystem.block_count == 64);
	memset(buffer, 0xa5, sizeof(buffer));
	assert(adapter.filesystem.write_block(adapter.filesystem.context, 3,
	    buffer) == 0);
	assert(disk.last_lba == (uint64_t)3 * (4096 / sector_size));
	assert(disk.last_count == 4096 / sector_size);
	memset(buffer, 0, sizeof(buffer));
	assert(adapter.filesystem.read_block(adapter.filesystem.context, 3,
	    buffer) == 0);
	assert(buffer[0] == 0xa5 && buffer[4095] == 0xa5);
	assert(block_read(&block, 0, 1, buffer + 1) == -EINVAL);
	assert(block_read(&block, block.sector_count, 1, buffer) == -EINVAL);
	disk.failure = 1;
	assert(adapter.filesystem.read_block(adapter.filesystem.context, 3,
	    buffer) == -EIO);
	assert(block_write(&block, 0, 1, buffer) == -EIO);
	assert(block_flush(&block) == -EIO);
}

int
main(void)
{
	block = (struct block_device){
		.name = "disk-test",
		.logical_sector_size = 512,
		.sector_count = sizeof(disk.bytes) / 512,
		.operations = &operations,
		.context = &disk
	};
	assert(block_device_register(&block) == 0);
	test_sector_size(512);
	disk.failure = 0;
	test_sector_size(4096);
	block.operations = &no_flush_operations;
	assert(block_flush(&block) == -EOPNOTSUPP);
	puts("block-sector tests passed");
	return 0;
}
