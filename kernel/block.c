#include <string.h>

#include "kernel/block.h"
#include "kernel/errno.h"

static struct block_device *devices[BLOCK_DEVICE_MAX];
static size_t device_count;

int
block_device_register(struct block_device *device)
{
	size_t i;

	if (device == NULL || device->name[0] == '\0' ||
	    memchr(device->name, '\0', sizeof(device->name)) == NULL ||
	    (device->logical_sector_size != 512 &&
	    device->logical_sector_size != 4096) || device->sector_count == 0 ||
	    device->sector_count > UINT64_MAX / device->logical_sector_size ||
	    device->operations == NULL || device->operations->read == NULL ||
	    device->operations->write == NULL || device->registered)
		return -EINVAL;
	for (i = 0; i < device_count; i++)
		if (devices[i] == device || strcmp(devices[i]->name, device->name) == 0)
			return -EEXIST;
	if (device_count == BLOCK_DEVICE_MAX)
		return -ENOSPC;
	devices[device_count++] = device;
	device->registered = true;
	return 0;
}

size_t
block_device_count(void)
{
	return device_count;
}

struct block_device *
block_device_at(size_t index)
{
	return index < device_count ? devices[index] : NULL;
}

static int
validate_io(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, const void *buffer)
{
	if (device == NULL || !device->registered || buffer == NULL ||
	    sector_count == 0 || ((uintptr_t)buffer % device->logical_sector_size) != 0)
		return -EINVAL;
	if (first_sector >= device->sector_count ||
	    sector_count > device->sector_count - first_sector)
		return -EINVAL;
	if ((uint64_t)sector_count > SIZE_MAX / device->logical_sector_size)
		return -EOVERFLOW;
	return 0;
}

int
block_read(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, void *buffer)
{
	int error = validate_io(device, first_sector, sector_count, buffer);
	if (error != 0)
		return error;
	error = device->operations->read(device, first_sector, sector_count, buffer);
	return error <= 0 ? error : -EIO;
}

int
block_write(struct block_device *device, uint64_t first_sector,
	uint32_t sector_count, const void *buffer)
{
	int error = validate_io(device, first_sector, sector_count, buffer);
	if (error != 0)
		return error;
	error = device->operations->write(device, first_sector, sector_count, buffer);
	return error <= 0 ? error : -EIO;
}

int
block_flush(struct block_device *device)
{
	int error;

	if (device == NULL || !device->registered)
		return -EINVAL;
	if (device->operations->flush == NULL)
		return -EOPNOTSUPP;
	error = device->operations->flush(device);
	return error <= 0 ? error : -EIO;
}
