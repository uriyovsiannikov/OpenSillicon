#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/silliconfs.h"
#include "kernel/vfs.h"

struct memory_device {
	uint8_t *bytes;
	uint32_t blocks;
	uint32_t fail_read;
	uint32_t fail_write;
};

void *
kheap_alloc(size_t size)
{
	return malloc(size);
}

bool
kheap_free(void *allocation)
{
	free(allocation);
	return true;
}

bool
serial_write(const char *buffer, size_t length)
{
	return fwrite(buffer, 1, length, stdout) == length;
}

void
klog(const char *message)
{
	fputs(message, stderr);
}

void
klog_u64(uint64_t value)
{
	(void)value;
}

_Noreturn void
kernel_halt(void)
{
	abort();
}

static int
memory_read(void *context, uint32_t number, void *block)
{
	struct memory_device *device = context;

	if (number >= device->blocks || number == device->fail_read)
		return -5;
	memcpy(block, device->bytes + (size_t)number * SILLICONFS_BLOCK_SIZE,
	    SILLICONFS_BLOCK_SIZE);
	return 0;
}

static int
memory_write(void *context, uint32_t number, const void *block)
{
	struct memory_device *device = context;

	if (number >= device->blocks || number == device->fail_write)
		return -5;
	memcpy(device->bytes + (size_t)number * SILLICONFS_BLOCK_SIZE, block,
	    SILLICONFS_BLOCK_SIZE);
	return 0;
}

static struct silliconfs_device
make_device(struct memory_device *memory)
{
	return (struct silliconfs_device){
		.context = memory,
		.block_count = memory->blocks,
		.read_block = memory_read,
		.write_block = memory_write
	};
}

static uint8_t *
load_image(const char *path, size_t *length)
{
	FILE *file = fopen(path, "rb");
	long size;
	uint8_t *bytes;

	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	size = ftell(file);
	assert(size > 0 && size % SILLICONFS_BLOCK_SIZE == 0);
	assert(fseek(file, 0, SEEK_SET) == 0);
	bytes = malloc((size_t)size);
	assert(bytes != NULL);
	assert(fread(bytes, 1, (size_t)size, file) == (size_t)size);
	assert(fclose(file) == 0);
	*length = (size_t)size;
	return bytes;
}

static void
save_image(const char *path, const uint8_t *bytes, size_t length)
{
	FILE *file = fopen(path, "wb");

	assert(file != NULL);
	assert(fwrite(bytes, 1, length, file) == length);
	assert(fclose(file) == 0);
}

static void
reject_copy(const uint8_t *image, size_t size, size_t offset, uint8_t value,
	uint32_t blocks)
{
	struct memory_device memory = { .blocks = blocks, .fail_read = UINT32_MAX,
	    .fail_write = UINT32_MAX };
	struct silliconfs_device device;
	struct vnode *root = NULL;

	memory.bytes = malloc(size);
	assert(memory.bytes != NULL);
	memcpy(memory.bytes, image, size);
	memory.bytes[offset] = value;
	device = make_device(&memory);
	assert(silliconfs_mount(&device, &root) != 0);
	assert(root == NULL);
	free(memory.bytes);
}

static uint32_t
superblock_crc(const uint8_t *bytes)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < SILLICONFS_BLOCK_SIZE; i++) {
		uint8_t value = i >= 72 && i < 76 ? 0 : bytes[i];

		crc ^= value;
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
	}
	return crc ^ UINT32_MAX;
}

static void
reject_superblock_field(const uint8_t *image, size_t size, size_t offset,
	uint32_t value, uint32_t blocks)
{
	struct memory_device memory = { .blocks = blocks, .fail_read = UINT32_MAX,
	    .fail_write = UINT32_MAX };
	struct silliconfs_device device;
	struct vnode *root = NULL;
	uint32_t checksum;

	memory.bytes = malloc(size);
	assert(memory.bytes != NULL);
	memcpy(memory.bytes, image, size);
	memcpy(memory.bytes + offset, &value, sizeof(value));
	memset(memory.bytes + 72, 0, 4);
	checksum = superblock_crc(memory.bytes);
	memcpy(memory.bytes + 72, &checksum, sizeof(checksum));
	device = make_device(&memory);
	assert(silliconfs_mount(&device, &root) != 0);
	assert(root == NULL);
	free(memory.bytes);
}

static void
test_malformed_images(const uint8_t *image, size_t size, uint32_t blocks)
{
	struct memory_device memory = { .bytes = (uint8_t *)image,
	    .blocks = blocks, .fail_read = UINT32_MAX,
	    .fail_write = UINT32_MAX };
	struct silliconfs_device device = make_device(&memory);
	struct vnode *root = NULL;
	uint32_t data_start = 7;

	reject_copy(image, size, 0, 'X', blocks);
	reject_copy(image, size, 72, (uint8_t)(image[72] ^ 1), blocks);
	reject_copy(image, size, 40, 0, blocks);
	reject_copy(image, size, SILLICONFS_BLOCK_SIZE, 0x7f, blocks);
	reject_copy(image, size, 2 * SILLICONFS_BLOCK_SIZE, 0, blocks);
	reject_superblock_field(image, size, 16, 1, blocks);
	reject_superblock_field(image, size, 64, 6, blocks);
	reject_copy(image, size, SILLICONFS_BLOCK_SIZE * 3 + 76, 1, blocks);
	reject_copy(image, size, (size_t)data_start * SILLICONFS_BLOCK_SIZE + 4,
	    0, blocks);
	reject_copy(image, size, (size_t)data_start * SILLICONFS_BLOCK_SIZE + 12,
	    2, blocks);
	{
		uint8_t *copy = malloc(size);
		struct memory_device corrupt;
		struct vnode *invalid_root = NULL;
		uint32_t root_direct_offset = 3 * SILLICONFS_BLOCK_SIZE + 24;

		assert(copy != NULL);
		memcpy(copy, image, size);
		memset(copy + root_direct_offset, 0xff, 4);
		corrupt = (struct memory_device){ .bytes = copy, .blocks = blocks,
		    .fail_read = UINT32_MAX, .fail_write = UINT32_MAX };
		device = make_device(&corrupt);
		assert(silliconfs_mount(&device, &invalid_root) != 0);
		assert(invalid_root == NULL);
		free(copy);
	}
	/* A data block may be referenced by only one inode pointer. */
	reject_copy(image, size, 3 * SILLICONFS_BLOCK_SIZE + 28,
	    (uint8_t)data_start, blocks);
	memory.fail_read = 0;
	assert(silliconfs_mount(&device, &root) != 0);
	assert(root == NULL);
}

static void
test_vfs_disk_image(const char *path, uint8_t *disk, size_t size,
	uint32_t blocks)
{
	struct memory_device memory = { .bytes = disk, .blocks = blocks,
	    .fail_read = UINT32_MAX, .fail_write = UINT32_MAX };
	struct silliconfs_device device = make_device(&memory);
	struct vnode *root;
	struct vnode *created;
	struct fd_object *file;
	struct vfs_stat status;
	struct vfs_dirent entry;
	uint8_t *buffer;
	size_t amount;
	uint64_t indirect_offset = 12ULL * SILLICONFS_BLOCK_SIZE + 7;
	const char payload[] = "indirect data";

	assert(vfs_init());
	{
		int result = silliconfs_mount(&device, &root);
		if (result != 0)
			fprintf(stderr, "valid SilliconFS mount returned %d\n", result);
		assert(result == 0);
	}
	assert(root != NULL && root->type == VNODE_DIRECTORY);
	assert(vfs_mount("/", root) == 0);
	vnode_put(root);
	{
		int result = vfs_create_path("/docs", VNODE_DIRECTORY, 0750, &created);
		if (result != 0)
			fprintf(stderr, "create /docs returned %d\n", result);
		assert(result == 0);
	}
	assert(created->mode == 0750);
	vnode_put(created);
	assert(vfs_create_path("/docs/data", VNODE_REGULAR, 0640,
	    &created) == 0);
	assert(created->mode == 0640);
	vnode_put(created);
	assert(vfs_open("/docs/data", VFS_O_RDWR, 0, &file) == 0);
	assert(fd_object_write(file, payload, sizeof(payload), &amount) == 0);
	assert(amount == sizeof(payload));
	file->offset = indirect_offset;
	assert(fd_object_write(file, "tail", 4, &amount) == 0 && amount == 4);
	assert(vfs_stat_path("/docs/data", &status) == 0);
	assert(status.size == indirect_offset + 4 && status.mode == 0640);
	buffer = malloc((size_t)status.size);
	assert(buffer != NULL);
	file->offset = 0;
	assert(fd_object_read(file, buffer, (size_t)status.size, &amount) == 0);
	assert(amount == status.size);
	assert(memcmp(buffer, payload, sizeof(payload)) == 0);
	for (size_t i = sizeof(payload); i < indirect_offset; i++)
		assert(buffer[i] == 0);
	assert(memcmp(buffer + indirect_offset, "tail", 4) == 0);
	free(buffer);
	file->offset = SILLICONFS_MAX_FILE_SIZE - 1;
	assert(fd_object_write(file, "Z", 1, &amount) == 0 && amount == 1);
	assert(vfs_stat_path("/docs/data", &status) == 0);
	assert(status.size == SILLICONFS_MAX_FILE_SIZE);
	file->offset = SILLICONFS_MAX_FILE_SIZE;
	assert(fd_object_write(file, "x", 1, &amount) == -75);
	assert(fd_object_write(file, NULL, 0, &amount) == 0 && amount == 0);
	assert(vfs_stat_path("/docs/data", &status) == 0);
	assert(status.size == SILLICONFS_MAX_FILE_SIZE);
	fd_object_put(file);
	assert(vfs_open("/docs", VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &file) == 0);
	assert(fd_readdir(&(struct fd_table){ .entries = { [3] = file } }, 3,
	    &entry) == 1);
	assert(strcmp(entry.name, ".") == 0);
	fd_object_put(file);
	assert(vfs_open("/docs/data", VFS_O_RDWR | VFS_O_TRUNC, 0, &file) == 0);
	assert(vfs_stat_path("/docs/data", &status) == 0 && status.size == 0);
	assert(fd_object_write(file, "persisted", 9, &amount) == 0 && amount == 9);
	fd_object_put(file);
	memory.fail_write = 9;
	assert(vfs_open("/docs/data", VFS_O_RDWR, 0, &file) == 0);
	assert(fd_object_write(file, "X", 1, &amount) == -5);
	fd_object_put(file);
	memory.fail_write = UINT32_MAX;

	save_image(path, disk, size);
	{
		uint8_t *reloaded;
		size_t reloaded_size;
		struct memory_device second;
		struct silliconfs_device second_device;
		struct vnode *second_root;
		struct vnode *docs;
		struct vnode *data;
		char persisted[9];

		reloaded = load_image(path, &reloaded_size);
		assert(reloaded_size == size);
		second = (struct memory_device){ .bytes = reloaded, .blocks = blocks,
		    .fail_read = UINT32_MAX, .fail_write = UINT32_MAX };
		second_device = make_device(&second);
		assert(silliconfs_mount(&second_device, &second_root) == 0);
		assert(second_root->operations->lookup(second_root, "docs", 4,
		    &docs) == 0);
		assert(docs->operations->lookup(docs, "data", 4, &data) == 0);
		assert(data->operations->read(data, 0, persisted, sizeof(persisted),
		    &amount) == 0 && amount == sizeof(persisted));
		assert(memcmp(persisted, "persisted", sizeof(persisted)) == 0);
		vnode_put(data);
		vnode_put(docs);
		vnode_put(second_root);
		free(reloaded);
	}
}

int
main(int argc, char **argv)
{
	uint8_t *image;
	size_t size;
	uint32_t blocks;

	assert(argc == 2);
	image = load_image(argv[1], &size);
	blocks = (uint32_t)(size / SILLICONFS_BLOCK_SIZE);
	test_malformed_images(image, size, blocks);
	test_vfs_disk_image(argv[1], image, size, blocks);
	free(image);
	puts("SilliconFS formatting, validation, VFS I/O, remount, and corruption tests passed");
	return 0;
}
