#include <stdint.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/silliconfs.h"

#define SFS_EIO 5
#define SFS_EBADF 9
#define SFS_EEXIST 17
#define SFS_EINVAL 22
#define SFS_EISDIR 21
#define SFS_ENOENT 2
#define SFS_ENOMEM 12
#define SFS_ENOSPC 28
#define SFS_ENOTDIR 20
#define SFS_EOVERFLOW 75
#define SFS_DIRECT 12U
#define SFS_INDIRECT_COUNT 1024U
#define SFS_ROOT_INODE 1U
#define SFS_MODE_TYPE 0170000U
#define SFS_MODE_REGULAR 0100000U
#define SFS_MODE_DIRECTORY 0040000U
#define SFS_DIR_HEADER 8U

struct sfs_superblock {
	uint32_t total_blocks;
	uint32_t inode_count;
	uint32_t block_bitmap_start;
	uint32_t block_bitmap_blocks;
	uint32_t inode_bitmap_start;
	uint32_t inode_bitmap_blocks;
	uint32_t inode_table_start;
	uint32_t inode_table_blocks;
	uint32_t data_start;
};

struct sfs_inode {
	uint16_t mode;
	uint16_t flags;
	uint32_t uid;
	uint32_t gid;
	uint64_t size;
	uint32_t direct[SFS_DIRECT];
	uint32_t indirect;
};

struct silliconfs {
	struct silliconfs_device device;
	struct sfs_superblock super;
	uint32_t references;
};

struct sfs_node {
	struct vnode vnode;
	struct silliconfs *filesystem;
	uint32_t inode_number;
};

struct sfs_name_slot {
	uint32_t hash;
	uint32_t offset;
};

static int sfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int sfs_create(struct vnode *, const char *, size_t, enum vnode_type,
	uint32_t, uint32_t, uint32_t, struct vnode **);
static int sfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int sfs_write(struct vnode *, uint64_t, const void *, size_t, size_t *);
static int sfs_truncate(struct vnode *, uint64_t);
static int sfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void sfs_destroy(struct vnode *);
static int sfs_chmod(struct vnode *, uint32_t);
static int sfs_chown(struct vnode *, uint32_t, uint32_t);
static void sync_vnode(struct sfs_node *, const struct sfs_inode *);

static const struct vnode_operations sfs_operations = {
	.lookup = sfs_lookup,
	.create = sfs_create,
	.read = sfs_read,
	.write = sfs_write,
	.truncate = sfs_truncate,
	.readdir = sfs_readdir,
	.destroy = sfs_destroy,
	.chmod = sfs_chmod,
	.chown = sfs_chown
};

static uint16_t
get_le16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}


static uint32_t
get_le32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t
get_le64(const uint8_t *bytes)
{
	return (uint64_t)get_le32(bytes) | ((uint64_t)get_le32(bytes + 4) << 32);
}

static void
put_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void
put_le32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void
put_le64(uint8_t *bytes, uint64_t value)
{
	put_le32(bytes, (uint32_t)value);
	put_le32(bytes + 4, (uint32_t)(value >> 32));
}

static uint32_t
crc32_block(const uint8_t *bytes, size_t length)
{
	uint32_t crc = UINT32_MAX;
	size_t i;
	unsigned int bit;

	for (i = 0; i < length; i++) {
		uint8_t value = (i >= 72 && i < 76) ? 0 : bytes[i];

		crc ^= value;
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
	}
	return crc ^ UINT32_MAX;
}

static int
read_block(struct silliconfs *filesystem, uint32_t block, void *buffer)
{
	int error;

	if (block >= filesystem->super.total_blocks)
		return -SFS_EIO;
	error = filesystem->device.read_block(filesystem->device.context, block,
	    buffer);
	return error == 0 ? 0 : (error < 0 ? error : -SFS_EIO);
}

static int
write_block(struct silliconfs *filesystem, uint32_t block,
	const void *buffer)
{
	int error;

	if (block >= filesystem->super.total_blocks ||
	    filesystem->device.write_block == NULL)
		return -SFS_EIO;
	error = filesystem->device.write_block(filesystem->device.context, block,
	    buffer);
	return error == 0 ? 0 : (error < 0 ? error : -SFS_EIO);
}

static uint32_t
blocks_for(uint64_t count, uint64_t per_block)
{
	return (uint32_t)(count / per_block + (count % per_block != 0));
}

static bool
zero_bytes(const uint8_t *bytes, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		if (bytes[i] != 0)
			return false;
	}
	return true;
}

static int
load_superblock(struct silliconfs *filesystem)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	const uint8_t *bytes = block;
	uint32_t block_count;
	uint32_t inode_count;
	uint64_t expected;
	uint64_t end;
	int error;

	error = read_block(filesystem, 0, block);
	if (error != 0)
		return error;
	if (memcmp(bytes, "SILFS1\0\0", 8) != 0 || get_le16(bytes + 8) != 1 ||
	    get_le16(bytes + 10) != 80 || get_le32(bytes + 12) !=
	    SILLICONFS_BLOCK_SIZE || get_le32(bytes + 16) != 0 ||
	    get_le32(bytes + 20) != 0 || get_le32(bytes + 24) != 0 ||
	    get_le32(bytes + 36) != SFS_ROOT_INODE || get_le32(bytes + 68) != 0 ||
	    !zero_bytes(bytes + 76, SILLICONFS_BLOCK_SIZE - 76) ||
	    get_le32(bytes + 72) != crc32_block(bytes, sizeof(block)))
		return -SFS_EIO;
	block_count = get_le32(bytes + 28);
	inode_count = get_le32(bytes + 32);
	if (block_count != filesystem->device.block_count || block_count == 0 ||
	    inode_count == 0)
		return -SFS_EIO;
	filesystem->super = (struct sfs_superblock){
		.total_blocks = block_count,
		.inode_count = inode_count,
		.block_bitmap_start = get_le32(bytes + 40),
		.block_bitmap_blocks = get_le32(bytes + 44),
		.inode_bitmap_start = get_le32(bytes + 48),
		.inode_bitmap_blocks = get_le32(bytes + 52),
		.inode_table_start = get_le32(bytes + 56),
		.inode_table_blocks = get_le32(bytes + 60),
		.data_start = get_le32(bytes + 64)
	};
	if (filesystem->super.block_bitmap_start != 1 ||
	    filesystem->super.block_bitmap_blocks !=
	    blocks_for(block_count, 32768) ||
	    filesystem->super.inode_bitmap_start != 1 +
	    filesystem->super.block_bitmap_blocks ||
	    filesystem->super.inode_bitmap_blocks !=
	    blocks_for(inode_count, 32768) ||
	    filesystem->super.inode_table_start !=
	    filesystem->super.inode_bitmap_start +
	    filesystem->super.inode_bitmap_blocks ||
	    filesystem->super.inode_table_blocks != blocks_for(inode_count, 32))
		return -SFS_EIO;
	expected = (uint64_t)filesystem->super.inode_table_start +
	    filesystem->super.inode_table_blocks;
	end = expected + 1;
	if (expected > UINT32_MAX || end > block_count ||
	    filesystem->super.data_start != expected)
		return -SFS_EIO;
	return 0;
}

static int
bitmap_get(struct silliconfs *filesystem, bool inode_map, uint32_t bit,
	bool *allocated)
{
	uint32_t start = inode_map ? filesystem->super.inode_bitmap_start :
	    filesystem->super.block_bitmap_start;
	uint32_t block_number = start + bit / 32768U;
	uint32_t within = bit % 32768U;
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	int error;

	if (allocated == NULL || block_number >= filesystem->super.total_blocks)
		return -SFS_EIO;
	error = read_block(filesystem, block_number, block);
	if (error != 0)
		return error;
	*allocated = (block[within / 8] & (1U << (within % 8))) != 0;
	return 0;
}

static int
bitmap_set(struct silliconfs *filesystem, bool inode_map, uint32_t bit,
	bool allocated)
{
	uint32_t start = inode_map ? filesystem->super.inode_bitmap_start :
	    filesystem->super.block_bitmap_start;
	uint32_t block_number = start + bit / 32768U;
	uint32_t within = bit % 32768U;
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint8_t mask = (uint8_t)(1U << (within % 8));
	int error = read_block(filesystem, block_number, block);

	if (error != 0)
		return error;
	if (allocated)
		block[within / 8] |= mask;
	else
		block[within / 8] &= (uint8_t)~mask;
	return write_block(filesystem, block_number, block);
}

static int
block_allocated(struct silliconfs *filesystem, uint32_t block, bool *allocated)
{
	if (block < filesystem->super.data_start ||
	    block >= filesystem->super.total_blocks)
		return -SFS_EIO;
	return bitmap_get(filesystem, false, block, allocated);
}

static int
read_inode(struct silliconfs *filesystem, uint32_t number,
	struct sfs_inode *inode)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint64_t record_offset;
	uint32_t block_number;
	uint32_t offset;
	bool allocated;
	const uint8_t *record;
	int error;

	if (number == 0 || number > filesystem->super.inode_count || inode == NULL)
		return -SFS_EIO;
	error = bitmap_get(filesystem, true, number - 1, &allocated);
	if (error != 0 || !allocated)
		return error != 0 ? error : -SFS_EIO;
	record_offset = (uint64_t)(number - 1) * 128;
	block_number = filesystem->super.inode_table_start +
	    (uint32_t)(record_offset / SILLICONFS_BLOCK_SIZE);
	offset = (uint32_t)(record_offset % SILLICONFS_BLOCK_SIZE);
	error = read_block(filesystem, block_number, block);
	if (error != 0)
		return error;
	record = block + offset;
	inode->mode = get_le16(record);
	inode->flags = get_le16(record + 2);
	inode->uid = get_le32(record + 4);
	inode->gid = get_le32(record + 8);
	inode->size = get_le64(record + 16);
	for (unsigned int i = 0; i < SFS_DIRECT; i++)
		inode->direct[i] = get_le32(record + 24 + i * 4);
	inode->indirect = get_le32(record + 72);
	if (get_le32(record + 12) != 0 || !zero_bytes(record + 76, 52) ||
	    inode->flags != 0 || inode->size > SILLICONFS_MAX_FILE_SIZE ||
	    ((inode->mode & SFS_MODE_TYPE) != SFS_MODE_REGULAR &&
	    (inode->mode & SFS_MODE_TYPE) != SFS_MODE_DIRECTORY) ||
	    (inode->mode & ~(SFS_MODE_TYPE | 07777U)) != 0 ||
	    ((inode->mode & SFS_MODE_TYPE) == SFS_MODE_DIRECTORY &&
	    (inode->size == 0 || inode->size % SILLICONFS_BLOCK_SIZE != 0)))
		return -SFS_EIO;
	return 0;
}

static int
write_inode(struct silliconfs *filesystem, uint32_t number,
	const struct sfs_inode *inode)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint64_t record_offset = (uint64_t)(number - 1) * 128;
	uint32_t block_number = filesystem->super.inode_table_start +
	    (uint32_t)(record_offset / SILLICONFS_BLOCK_SIZE);
	uint32_t offset = (uint32_t)(record_offset % SILLICONFS_BLOCK_SIZE);
	uint8_t *record;
	int error;

	if (number == 0 || number > filesystem->super.inode_count)
		return -SFS_EINVAL;
	error = read_block(filesystem, block_number, block);
	if (error != 0)
		return error;
	record = block + offset;
	memset(record, 0, 128);
	put_le16(record, inode->mode);
	put_le16(record + 2, inode->flags);
	put_le32(record + 4, inode->uid);
	put_le32(record + 8, inode->gid);
	put_le64(record + 16, inode->size);
	for (unsigned int i = 0; i < SFS_DIRECT; i++)
		put_le32(record + 24 + i * 4, inode->direct[i]);
	put_le32(record + 72, inode->indirect);
	return write_block(filesystem, block_number, block);
}

static int
mark_referenced_block(struct silliconfs *filesystem, uint8_t *seen,
	uint32_t block)
{
	bool allocated;
	uint8_t mask;
	int error = block_allocated(filesystem, block, &allocated);

	if (error != 0 || !allocated)
		return error != 0 ? error : -SFS_EIO;
	mask = (uint8_t)(1U << (block % 8));
	if ((seen[block / 8] & mask) != 0)
		return -SFS_EIO;
	seen[block / 8] |= mask;
	return 0;
}

static int
validate_inode_blocks(struct silliconfs *filesystem,
	const struct sfs_inode *inode, uint8_t *seen_blocks)
{
	uint8_t indirect[SILLICONFS_BLOCK_SIZE];
	int error;

	for (unsigned int i = 0; i < SFS_DIRECT; i++) {
		if (inode->direct[i] == 0)
			continue;
		error = mark_referenced_block(filesystem, seen_blocks,
		    inode->direct[i]);
		if (error != 0)
			return error;
	}
	if (inode->indirect == 0)
		return 0;
	error = mark_referenced_block(filesystem, seen_blocks, inode->indirect);
	if (error != 0)
		return error;
	error = read_block(filesystem, inode->indirect, indirect);
	if (error != 0)
		return error;
	for (unsigned int i = 0; i < SFS_INDIRECT_COUNT; i++) {
		uint32_t block = get_le32(indirect + i * 4);

		if (block == 0)
			continue;
		error = mark_referenced_block(filesystem, seen_blocks, block);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
read_logical_block(struct silliconfs *filesystem,
	const struct sfs_inode *inode, uint32_t logical, uint8_t *block,
	uint32_t *physical)
{
	uint32_t number = 0;
	uint8_t indirect[SILLICONFS_BLOCK_SIZE];
	int error;

	if (logical >= 1036)
		return -SFS_EIO;
	if (logical < SFS_DIRECT) {
		number = inode->direct[logical];
	} else if (inode->indirect != 0) {
		error = read_block(filesystem, inode->indirect, indirect);
		if (error != 0)
			return error;
		number = get_le32(indirect + (logical - SFS_DIRECT) * 4);
	}
	if (physical != NULL)
		*physical = number;
	if (number == 0) {
		memset(block, 0, SILLICONFS_BLOCK_SIZE);
		return 0;
	}
	return read_block(filesystem, number, block);
}

static int
allocate_block(struct silliconfs *filesystem, uint32_t *result)
{
	uint32_t block;
	bool allocated;
	uint8_t zero[SILLICONFS_BLOCK_SIZE];
	int error;

	memset(zero, 0, sizeof(zero));
	for (block = filesystem->super.data_start;
	    block < filesystem->super.total_blocks; block++) {
		error = bitmap_get(filesystem, false, block, &allocated);
		if (error != 0)
			return error;
		if (allocated)
			continue;
		error = bitmap_set(filesystem, false, block, true);
		if (error != 0)
			return error;
		error = write_block(filesystem, block, zero);
		if (error != 0) {
			(void)bitmap_set(filesystem, false, block, false);
			return error;
		}
		*result = block;
		return 0;
	}
	return -SFS_ENOSPC;
}

static int
free_block(struct silliconfs *filesystem, uint32_t block)
{
	bool allocated;
	int error = block_allocated(filesystem, block, &allocated);

	if (error != 0 || !allocated)
		return error != 0 ? error : -SFS_EIO;
	return bitmap_set(filesystem, false, block, false);
}

static int
allocate_inode(struct silliconfs *filesystem, uint32_t *result)
{
	uint64_t candidate;
	bool allocated;
	struct sfs_inode empty = { 0 };
	int error;

	for (candidate = 2; candidate <= filesystem->super.inode_count;
	    candidate++) {
		uint32_t number = (uint32_t)candidate;
		error = bitmap_get(filesystem, true, number - 1, &allocated);
		if (error != 0)
			return error;
		if (allocated)
			continue;
		error = bitmap_set(filesystem, true, number - 1, true);
		if (error != 0)
			return error;
		error = write_inode(filesystem, number, &empty);
		if (error != 0) {
			(void)bitmap_set(filesystem, true, number - 1, false);
			return error;
		}
		*result = number;
		return 0;
	}
	return -SFS_ENOSPC;
}

static int
free_inode(struct silliconfs *filesystem, uint32_t number)
{
	struct sfs_inode empty = { 0 };
	int error = write_inode(filesystem, number, &empty);

	if (error != 0)
		return error;
	return bitmap_set(filesystem, true, number - 1, false);
}

static int
map_block(struct silliconfs *filesystem, uint32_t inode_number,
	struct sfs_inode *inode, uint32_t logical, bool create, uint32_t *physical)
{
	uint8_t indirect[SILLICONFS_BLOCK_SIZE];
	uint32_t block;
	int error;

	if (logical >= 1036 || physical == NULL)
		return -SFS_EOVERFLOW;
	if (logical < SFS_DIRECT) {
		block = inode->direct[logical];
		if (block == 0 && create) {
			error = allocate_block(filesystem, &block);
			if (error != 0)
				return error;
			inode->direct[logical] = block;
			error = write_inode(filesystem, inode_number, inode);
			if (error != 0)
				return error;
		}
		*physical = block;
		return 0;
	}
	if (inode->indirect == 0 && create) {
		error = allocate_block(filesystem, &inode->indirect);
		if (error != 0)
			return error;
		error = write_inode(filesystem, inode_number, inode);
		if (error != 0)
			return error;
	}
	if (inode->indirect == 0) {
		*physical = 0;
		return 0;
	}
	error = read_block(filesystem, inode->indirect, indirect);
	if (error != 0)
		return error;
	block = get_le32(indirect + (logical - SFS_DIRECT) * 4);
	if (block == 0 && create) {
		error = allocate_block(filesystem, &block);
		if (error != 0)
			return error;
		put_le32(indirect + (logical - SFS_DIRECT) * 4, block);
		error = write_block(filesystem, inode->indirect, indirect);
		if (error != 0)
			return error;
	}
	*physical = block;
	return 0;
}

static int
read_inode_data(struct silliconfs *filesystem, uint32_t inode_number,
	struct sfs_inode *inode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	size_t total = 0;

	if ((inode->mode & SFS_MODE_TYPE) != SFS_MODE_REGULAR)
		return -SFS_EISDIR;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -SFS_EINVAL;
	if (offset >= inode->size || length == 0) {
		*read_length = 0;
		return 0;
	}
	if ((uint64_t)length > inode->size - offset)
		length = (size_t)(inode->size - offset);
	while (total < length) {
		uint64_t position = offset + total;
		uint32_t logical = (uint32_t)(position / SILLICONFS_BLOCK_SIZE);
		size_t within = (size_t)(position % SILLICONFS_BLOCK_SIZE);
		size_t amount = SILLICONFS_BLOCK_SIZE - within;
		int error;

		if (amount > length - total)
			amount = length - total;
		error = read_logical_block(filesystem, inode, logical, block, NULL);
		if (error != 0)
			return error;
		memcpy((uint8_t *)buffer + total, block + within, amount);
		total += amount;
	}
	(void)inode_number;
	*read_length = total;
	return 0;
}

static int
write_inode_data(struct silliconfs *filesystem, uint32_t inode_number,
	struct sfs_inode *inode, uint64_t offset, const void *buffer, size_t length,
	size_t *write_length)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	size_t total = 0;
	uint64_t end;

	if ((inode->mode & SFS_MODE_TYPE) != SFS_MODE_REGULAR)
		return -SFS_EISDIR;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -SFS_EINVAL;
	if (length == 0) {
		*write_length = 0;
		return 0;
	}
	if (offset > SILLICONFS_MAX_FILE_SIZE ||
	    length > SILLICONFS_MAX_FILE_SIZE - offset)
		return -SFS_EOVERFLOW;
	end = offset + length;
	while (total < length) {
		uint64_t position = offset + total;
		uint32_t logical = (uint32_t)(position / SILLICONFS_BLOCK_SIZE);
		uint32_t physical;
		size_t within = (size_t)(position % SILLICONFS_BLOCK_SIZE);
		size_t amount = SILLICONFS_BLOCK_SIZE - within;
		int error = map_block(filesystem, inode_number, inode, logical, true,
		    &physical);

		if (error != 0)
			return error;
		if (amount > length - total)
			amount = length - total;
		error = read_block(filesystem, physical, block);
		if (error != 0)
			return error;
		memcpy(block + within, (const uint8_t *)buffer + total, amount);
		error = write_block(filesystem, physical, block);
		if (error != 0)
			return error;
		total += amount;
	}
	if (end > inode->size) {
		inode->size = end;
		if (write_inode(filesystem, inode_number, inode) != 0)
			return -SFS_EIO;
	}
	*write_length = total;
	return 0;
}

static uint32_t
name_hash(const uint8_t *name, size_t length)
{
	uint32_t hash = 2166136261U;

	for (size_t i = 0; i < length; i++) {
		hash ^= name[i];
		hash *= 16777619U;
	}
	return hash == 0 ? 1 : hash;
}

static uint32_t
name_table_size(uint64_t maximum_entries)
{
	uint64_t needed = maximum_entries * 2;
	uint32_t size = 1;

	while (size < needed && size <= UINT32_MAX / 2)
		size <<= 1;
	return size >= needed ? size : 0;
}

static bool
name_is(const uint8_t *name, uint8_t length, const char *literal)
{
	size_t literal_length = strlen(literal);

	return length == literal_length && memcmp(name, literal, length) == 0;
}

static int
validate_directory(struct silliconfs *filesystem, uint32_t number,
	const struct sfs_inode *inode, uint8_t *references, uint32_t *parents,
	uint32_t *dotdot)
{
	uint8_t *contents;
	struct sfs_name_slot *slots;
	uint32_t slot_count;
	uint64_t max_entries = inode->size / 12;
	uint64_t position = 0;
	bool dot_found = false;
	bool dotdot_found = false;
	int error = 0;

	if (inode->size > SIZE_MAX)
		return -SFS_EIO;
	slot_count = name_table_size(max_entries);
	if (slot_count == 0)
		return -SFS_EIO;
	contents = kheap_alloc((size_t)inode->size);
	slots = kheap_alloc((size_t)slot_count * sizeof(*slots));
	if (contents == NULL || slots == NULL) {
		(void)kheap_free(contents);
		(void)kheap_free(slots);
		return -SFS_ENOMEM;
	}
	memset(slots, 0, (size_t)slot_count * sizeof(*slots));
	for (uint32_t logical = 0; logical < inode->size / SILLICONFS_BLOCK_SIZE;
	    logical++) {
		uint32_t physical;
		error = read_logical_block(filesystem, inode, logical,
		    contents + (size_t)logical * SILLICONFS_BLOCK_SIZE, &physical);
		if (error != 0 || physical == 0) {
			error = -SFS_EIO;
			goto done;
		}
	}
	while (position < inode->size) {
		uint64_t block_end = (position / SILLICONFS_BLOCK_SIZE + 1) *
		    SILLICONFS_BLOCK_SIZE;
		const uint8_t *record = contents + position;
		uint32_t target;
		uint16_t record_length;
		uint8_t name_length;
		uint8_t type;
		uint32_t hash;
		uint32_t slot;
		struct sfs_inode target_inode;

		if (block_end - position < SFS_DIR_HEADER) {
			error = -SFS_EIO;
			goto done;
		}
		target = get_le32(record);
		record_length = get_le16(record + 4);
		name_length = record[6];
		type = record[7];
		if (target == 0 || target > filesystem->super.inode_count ||
		    name_length == 0 ||
		    record_length < ((SFS_DIR_HEADER + name_length + 3U) & ~3U) ||
		    record_length % 4 != 0 || record_length > block_end - position ||
		    !zero_bytes(record + SFS_DIR_HEADER + name_length,
		    record_length - SFS_DIR_HEADER - name_length) ||
		    (type != VNODE_REGULAR && type != VNODE_DIRECTORY)) {
			error = -SFS_EIO;
			goto done;
		}
		for (unsigned int i = 0; i < name_length; i++) {
			if (record[SFS_DIR_HEADER + i] == 0 ||
			    record[SFS_DIR_HEADER + i] == '/') {
				error = -SFS_EIO;
				goto done;
			}
		}
		if (read_inode(filesystem, target, &target_inode) != 0 ||
		    (target_inode.mode & SFS_MODE_TYPE) !=
		    (type == VNODE_DIRECTORY ? SFS_MODE_DIRECTORY : SFS_MODE_REGULAR)) {
			error = -SFS_EIO;
			goto done;
		}
		if (name_is(record + SFS_DIR_HEADER, name_length, ".")) {
			if (dot_found || target != number || type != VNODE_DIRECTORY) {
				error = -SFS_EIO;
				goto done;
			}
			dot_found = true;
		} else if (name_is(record + SFS_DIR_HEADER, name_length, "..")) {
			if (dotdot_found || type != VNODE_DIRECTORY) {
				error = -SFS_EIO;
				goto done;
			}
			dotdot_found = true;
			dotdot[number] = target;
		} else {
			if (references[target] != 0 || parents[target] != 0) {
				error = -SFS_EIO;
				goto done;
			}
			references[target] = 1;
			parents[target] = number;
		}
		hash = name_hash(record + SFS_DIR_HEADER, name_length);
		slot = hash & (slot_count - 1);
		while (slots[slot].hash != 0) {
			const uint8_t *previous = contents + slots[slot].offset;
			uint8_t previous_length = previous[6];

			if (slots[slot].hash == hash && previous_length == name_length &&
			    memcmp(previous + SFS_DIR_HEADER,
			    record + SFS_DIR_HEADER, name_length) == 0) {
				error = -SFS_EIO;
				goto done;
			}
			slot = (slot + 1) & (slot_count - 1);
		}
		slots[slot].hash = hash;
		slots[slot].offset = (uint32_t)position;
		position += record_length;
	}
	if (!dot_found || !dotdot_found || (number == SFS_ROOT_INODE &&
	    dotdot[number] != SFS_ROOT_INODE))
		error = -SFS_EIO;
done:
	(void)kheap_free(slots);
	(void)kheap_free(contents);
	return error;
}

static int
validate_filesystem(struct silliconfs *filesystem)
{
	uint32_t inode_count = filesystem->super.inode_count;
	uint8_t *references;
	uint32_t *parents;
	uint32_t *dotdot;
	uint8_t *seen_blocks;
	size_t seen_blocks_size = ((size_t)filesystem->super.total_blocks + 7) / 8;
	uint8_t bitmap[SILLICONFS_BLOCK_SIZE];
	bool allocated;
	int error;

	references = kheap_alloc((size_t)inode_count + 1);
	parents = kheap_alloc(((size_t)inode_count + 1) * sizeof(*parents));
	dotdot = kheap_alloc(((size_t)inode_count + 1) * sizeof(*dotdot));
	seen_blocks = kheap_alloc(seen_blocks_size);
	if (references == NULL || parents == NULL || dotdot == NULL ||
	    seen_blocks == NULL) {
		(void)kheap_free(references);
		(void)kheap_free(parents);
		(void)kheap_free(dotdot);
		(void)kheap_free(seen_blocks);
		return -SFS_ENOMEM;
	}
	memset(references, 0, (size_t)inode_count + 1);
	memset(parents, 0, ((size_t)inode_count + 1) * sizeof(*parents));
	memset(dotdot, 0, ((size_t)inode_count + 1) * sizeof(*dotdot));
	memset(seen_blocks, 0, seen_blocks_size);
	for (uint32_t block = 0; block < filesystem->super.block_bitmap_blocks;
	    block++) {
		error = read_block(filesystem,
		    filesystem->super.block_bitmap_start + block, bitmap);
		if (error != 0)
			goto done;
		for (uint32_t bit = 0; bit < 32768; bit++) {
			uint64_t number = (uint64_t)block * 32768 + bit;
			bool set = (bitmap[bit / 8] & (1U << (bit % 8))) != 0;

			if ((number >= filesystem->super.total_blocks ||
			    number < filesystem->super.data_start) && !set) {
				error = -SFS_EIO;
				goto done;
			}
		}
	}
	for (uint32_t block = 0; block < filesystem->super.inode_bitmap_blocks;
	    block++) {
		error = read_block(filesystem,
		    filesystem->super.inode_bitmap_start + block, bitmap);
		if (error != 0)
			goto done;
		for (uint32_t bit = 0; bit < 32768; bit++) {
			uint64_t number = (uint64_t)block * 32768 + bit;
			bool set = (bitmap[bit / 8] & (1U << (bit % 8))) != 0;

			if (number >= inode_count && !set) {
				error = -SFS_EIO;
				goto done;
			}
		}
	}
	for (uint32_t i = 0; i < filesystem->super.data_start; i++) {
		error = bitmap_get(filesystem, false, i, &allocated);
		if (error != 0 || !allocated) {
			error = -SFS_EIO;
			goto done;
		}
	}
	for (uint64_t candidate = 1; candidate <= inode_count; candidate++) {
		uint32_t number = (uint32_t)candidate;
		struct sfs_inode inode;
		uint8_t raw[SILLICONFS_BLOCK_SIZE];
		uint64_t record_offset = (uint64_t)(number - 1) * 128;
		uint32_t table_block = filesystem->super.inode_table_start +
		    (uint32_t)(record_offset / SILLICONFS_BLOCK_SIZE);
		uint32_t within = (uint32_t)(record_offset % SILLICONFS_BLOCK_SIZE);

		error = bitmap_get(filesystem, true, number - 1, &allocated);
		if (error != 0)
			goto done;
		error = read_block(filesystem, table_block, raw);
		if (error != 0)
			goto done;
		if (!allocated) {
			if (!zero_bytes(raw + within, 128)) {
				error = -SFS_EIO;
				goto done;
			}
			continue;
		}
		error = read_inode(filesystem, number, &inode);
		if (error != 0)
			goto done;
		error = validate_inode_blocks(filesystem, &inode, seen_blocks);
		if (error != 0)
			goto done;
	}
	{
		struct sfs_inode root;
		error = read_inode(filesystem, SFS_ROOT_INODE, &root);
		if (error != 0 || (root.mode & SFS_MODE_TYPE) != SFS_MODE_DIRECTORY) {
			error = -SFS_EIO;
			goto done;
		}
	}
	for (uint64_t candidate = 1; candidate <= inode_count; candidate++) {
		uint32_t number = (uint32_t)candidate;
		struct sfs_inode inode;

		error = bitmap_get(filesystem, true, number - 1, &allocated);
		if (error != 0)
			goto done;
		if (!allocated)
			continue;
		error = read_inode(filesystem, number, &inode);
		if (error != 0)
			goto done;
		if ((inode.mode & SFS_MODE_TYPE) != SFS_MODE_DIRECTORY)
			continue;
		error = validate_directory(filesystem, number, &inode, references,
		    parents, dotdot);
		if (error != 0)
			goto done;
	}
	for (uint64_t candidate = 1; candidate <= inode_count; candidate++) {
		uint32_t number = (uint32_t)candidate;
		struct sfs_inode inode;

		error = bitmap_get(filesystem, true, number - 1, &allocated);
		if (error != 0)
			goto done;
		if (!allocated)
			continue;
		error = read_inode(filesystem, number, &inode);
		if (error != 0)
			goto done;
		if (number == SFS_ROOT_INODE) {
			if (references[number] != 0 || parents[number] != 0 ||
			    dotdot[number] != SFS_ROOT_INODE) {
				error = -SFS_EIO;
				goto done;
			}
		} else if (references[number] != 1 || parents[number] == 0) {
			error = -SFS_EIO;
			goto done;
		}
		if (number != SFS_ROOT_INODE &&
		    (inode.mode & SFS_MODE_TYPE) == SFS_MODE_DIRECTORY &&
		    (parents[number] == 0 || dotdot[number] != parents[number])) {
			error = -SFS_EIO;
			goto done;
		}
	}
	references[SFS_ROOT_INODE] = 2;
	for (uint64_t candidate = 2; candidate <= inode_count; candidate++) {
		uint32_t number = (uint32_t)candidate;
		bool inode_allocated;
		uint32_t cursor;

		error = bitmap_get(filesystem, true, number - 1, &inode_allocated);
		if (error != 0)
			goto done;
		if (!inode_allocated)
			continue;
		cursor = number;
		while (cursor != SFS_ROOT_INODE && references[cursor] != 2) {
			if (references[cursor] == 3 || parents[cursor] == 0) {
				error = -SFS_EIO;
				goto done;
			}
			references[cursor] = 3;
			cursor = parents[cursor];
		}
		if (cursor != SFS_ROOT_INODE && references[cursor] != 2) {
			error = -SFS_EIO;
			goto done;
		}
		cursor = number;
		while (cursor != SFS_ROOT_INODE && references[cursor] == 3) {
			references[cursor] = 2;
			cursor = parents[cursor];
		}
	}
	error = 0;
done:
	(void)kheap_free(references);
	(void)kheap_free(parents);
	(void)kheap_free(dotdot);
	(void)kheap_free(seen_blocks);
	return error;
}

static struct sfs_node *
node_from_vnode(struct vnode *node)
{
	return (struct sfs_node *)node->private_data;
}

static int
sfs_chmod(struct vnode *vnode, uint32_t mode)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct sfs_inode inode;
	int error = read_inode(node->filesystem, node->inode_number, &inode);
	if (error != 0)
		return error;
	inode.mode = (uint16_t)((inode.mode & SFS_MODE_TYPE) | (mode & 07777U));
	error = write_inode(node->filesystem, node->inode_number, &inode);
	if (error == 0)
		sync_vnode(node, &inode);
	return error;
}

static int
sfs_chown(struct vnode *vnode, uint32_t uid, uint32_t gid)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct sfs_inode inode;
	int error = read_inode(node->filesystem, node->inode_number, &inode);
	if (error != 0)
		return error;
	inode.uid = uid;
	inode.gid = gid;
	inode.mode &= (uint16_t)~06000U;
	error = write_inode(node->filesystem, node->inode_number, &inode);
	if (error == 0)
		sync_vnode(node, &inode);
	return error;
}


static void
sync_vnode(struct sfs_node *node, const struct sfs_inode *inode)
{
	node->vnode.type = (inode->mode & SFS_MODE_TYPE) == SFS_MODE_DIRECTORY ?
	    VNODE_DIRECTORY : VNODE_REGULAR;
	node->vnode.mode = inode->mode & 07777U;
	node->vnode.uid = inode->uid;
	node->vnode.gid = inode->gid;
	node->vnode.size = inode->size;
}

static int
make_node(struct silliconfs *filesystem, uint32_t number,
	struct vnode **result)
{
	struct sfs_inode inode;
	struct sfs_node *node;
	int error = read_inode(filesystem, number, &inode);

	if (error != 0)
		return error;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return -SFS_ENOMEM;
	if (filesystem->references == UINT32_MAX) {
		(void)kheap_free(node);
		return -SFS_EOVERFLOW;
	}
	filesystem->references++;
	node->filesystem = filesystem;
	node->inode_number = number;
	node->vnode = (struct vnode){
		.operations = &sfs_operations,
		.inode = number,
		.references = 1,
		.private_data = node
	};
	sync_vnode(node, &inode);
	*result = &node->vnode;
	return 0;
}

static void
sfs_destroy(struct vnode *vnode)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct silliconfs *filesystem = node->filesystem;

	(void)kheap_free(node);
	if (--filesystem->references == 0)
		(void)kheap_free(filesystem);
}

static int
find_directory_entry(struct silliconfs *filesystem,
	const struct sfs_inode *directory, const char *name, size_t name_length,
	uint32_t *number, uint8_t *type)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint64_t position = 0;
	int error;

	while (position < directory->size) {
		uint32_t logical = (uint32_t)(position / SILLICONFS_BLOCK_SIZE);
		uint32_t physical;
		error = read_logical_block(filesystem, directory, logical, block,
		    &physical);
		if (error != 0 || physical == 0)
			return -SFS_EIO;
		for (uint32_t offset = 0; offset < SILLICONFS_BLOCK_SIZE;) {
			const uint8_t *record = block + offset;
			uint16_t record_length = get_le16(record + 4);
			uint8_t record_name_length = record[6];

			if (record_length < SFS_DIR_HEADER ||
			    record_length > SILLICONFS_BLOCK_SIZE - offset ||
			    record_length % 4 != 0 || record_name_length == 0 ||
			    record_length < ((SFS_DIR_HEADER + record_name_length + 3U) &
			    ~3U))
				return -SFS_EIO;
			if (record_name_length == name_length &&
			    memcmp(record + SFS_DIR_HEADER, name, name_length) == 0) {
				*number = get_le32(record);
				*type = record[7];
				return 0;
			}
			offset += record_length;
		}
		position += SILLICONFS_BLOCK_SIZE;
	}
	return -SFS_ENOENT;
}

static int
sfs_lookup(struct vnode *directory_node, const char *name, size_t name_length,
	struct vnode **result)
{
	struct sfs_node *node = node_from_vnode(directory_node);
	struct sfs_inode directory;
	uint32_t number;
	uint8_t type;
	int error;

	if (directory_node->type != VNODE_DIRECTORY)
		return -SFS_ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL)
		return -SFS_EINVAL;
	error = read_inode(node->filesystem, node->inode_number, &directory);
	if (error != 0)
		return error;
	error = find_directory_entry(node->filesystem, &directory, name,
	    name_length, &number, &type);
	if (error != 0)
		return error;
	return make_node(node->filesystem, number, result);
}

static uint16_t
align4_u16(uint32_t length)
{
	return (uint16_t)((length + 3U) & ~3U);
}

static void
write_dir_record(uint8_t *record, uint32_t number, uint16_t length,
	const char *name, uint8_t name_length, uint8_t type)
{
	put_le32(record, number);
	put_le16(record + 4, length);
	record[6] = name_length;
	record[7] = type;
	memcpy(record + SFS_DIR_HEADER, name, name_length);
	memset(record + SFS_DIR_HEADER + name_length, 0,
	    length - SFS_DIR_HEADER - name_length);
}

static int
append_directory_entry(struct silliconfs *filesystem, uint32_t parent_number,
	struct sfs_inode *parent, const char *name, size_t name_length,
	uint32_t child_number, uint8_t type)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint16_t needed = align4_u16(SFS_DIR_HEADER + (uint32_t)name_length);
	int error;

	for (uint32_t logical = 0; logical < parent->size / SILLICONFS_BLOCK_SIZE;
	    logical++) {
		uint32_t physical;
		error = read_logical_block(filesystem, parent, logical, block, &physical);
		if (error != 0 || physical == 0)
			return -SFS_EIO;
		for (uint32_t offset = 0; offset < SILLICONFS_BLOCK_SIZE;) {
			uint8_t *record = block + offset;
			uint16_t old_length = get_le16(record + 4);
			uint16_t minimal = align4_u16(SFS_DIR_HEADER + record[6]);

			if (old_length < minimal || old_length > SILLICONFS_BLOCK_SIZE - offset)
				return -SFS_EIO;
			if (old_length - minimal >= needed) {
				uint16_t new_length = old_length - minimal;

				put_le16(record + 4, minimal);
				write_dir_record(record + minimal, child_number, new_length,
				    name, (uint8_t)name_length, type);
				return write_block(filesystem, physical, block);
			}
			offset += old_length;
		}
	}
	{
		uint32_t physical;
		uint32_t logical = (uint32_t)(parent->size / SILLICONFS_BLOCK_SIZE);

		error = map_block(filesystem, parent_number, parent, logical, true,
		    &physical);
		if (error != 0)
			return error;
		memset(block, 0, sizeof(block));
		write_dir_record(block, child_number, SILLICONFS_BLOCK_SIZE, name,
		    (uint8_t)name_length, type);
		error = write_block(filesystem, physical, block);
		if (error != 0)
			return error;
		parent->size += SILLICONFS_BLOCK_SIZE;
		return write_inode(filesystem, parent_number, parent);
	}
}

static int
initialize_directory(struct silliconfs *filesystem, uint32_t number,
	uint32_t parent_number, uint32_t mode, uint32_t uid, uint32_t gid,
	struct sfs_inode *inode)
{
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint32_t physical;
	int error;

	memset(inode, 0, sizeof(*inode));
	error = allocate_block(filesystem, &physical);
	if (error != 0)
		return error;
	inode->mode = SFS_MODE_DIRECTORY | (mode & 07777U);
	inode->uid = uid;
	inode->gid = gid;
	inode->size = SILLICONFS_BLOCK_SIZE;
	inode->direct[0] = physical;
	error = write_inode(filesystem, number, inode);
	if (error != 0)
		return error;
	memset(block, 0, sizeof(block));
	write_dir_record(block, number, 12, ".", 1, VNODE_DIRECTORY);
	write_dir_record(block + 12, parent_number, SILLICONFS_BLOCK_SIZE - 12,
	    "..", 2, VNODE_DIRECTORY);
	return write_block(filesystem, physical, block);
}

static int
sfs_create(struct vnode *directory_node, const char *name, size_t name_length,
	enum vnode_type type, uint32_t mode, uint32_t uid, uint32_t gid,
	struct vnode **result)
{
	struct sfs_node *parent_node = node_from_vnode(directory_node);
	struct silliconfs *filesystem = parent_node->filesystem;
	struct sfs_inode parent;
	struct sfs_inode child;
	struct vnode *child_node = NULL;
	uint32_t child_number;
	uint8_t found_type;
	uint32_t found_number;
	int error;

	if (directory_node->type != VNODE_DIRECTORY)
		return -SFS_ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL || (type != VNODE_REGULAR && type != VNODE_DIRECTORY) ||
	    name_is((const uint8_t *)name, (uint8_t)name_length, ".") ||
	    name_is((const uint8_t *)name, (uint8_t)name_length, ".."))
		return -SFS_EINVAL;
	memset(&child, 0, sizeof(child));
	for (size_t i = 0; i < name_length; i++) {
		if (name[i] == '/' || name[i] == '\0')
			return -SFS_EINVAL;
	}
	error = read_inode(filesystem, parent_node->inode_number, &parent);
	if (error != 0)
		return error;
	error = find_directory_entry(filesystem, &parent, name, name_length,
	    &found_number, &found_type);
	if (error == 0)
		return -SFS_EEXIST;
	if (error != -SFS_ENOENT)
		return error;
	error = allocate_inode(filesystem, &child_number);
	if (error != 0)
		return error;
	if (type == VNODE_DIRECTORY) {
		error = initialize_directory(filesystem, child_number,
		    parent_node->inode_number, mode, uid, gid, &child);
	} else {
		child = (struct sfs_inode){
			.mode = SFS_MODE_REGULAR | (mode & 07777U),
			.uid = uid,
			.gid = gid
		};
		error = write_inode(filesystem, child_number, &child);
	}
	if (error != 0)
		goto rollback;
	error = make_node(filesystem, child_number, &child_node);
	if (error != 0)
		goto rollback;
	error = append_directory_entry(filesystem, parent_node->inode_number,
	    &parent, name, name_length, child_number,
	    type == VNODE_DIRECTORY ? VNODE_DIRECTORY : VNODE_REGULAR);
	if (error != 0) {
		vnode_put(child_node);
		goto rollback;
	}
	*result = child_node;
	return 0;
rollback:
	for (unsigned int i = 0; i < SFS_DIRECT; i++) {
		if (child.direct[i] != 0)
			(void)free_block(filesystem, child.direct[i]);
	}
	if (child.indirect != 0)
		(void)free_block(filesystem, child.indirect);
	(void)free_inode(filesystem, child_number);
	return error;
}

static int
sfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct sfs_inode inode;
	int error = read_inode(node->filesystem, node->inode_number, &inode);

	if (error != 0)
		return error;
	error = read_inode_data(node->filesystem, node->inode_number, &inode,
	    offset, buffer, length, read_length);
	if (error == 0)
		sync_vnode(node, &inode);
	return error;
}

static int
sfs_write(struct vnode *vnode, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct sfs_inode inode;
	int error = read_inode(node->filesystem, node->inode_number, &inode);

	if (error != 0)
		return error;
	error = write_inode_data(node->filesystem, node->inode_number, &inode,
	    offset, buffer, length, write_length);
	if (error == 0)
		sync_vnode(node, &inode);
	return error;
}

static int
sfs_truncate(struct vnode *vnode, uint64_t size)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct silliconfs *filesystem = node->filesystem;
	struct sfs_inode inode;
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	int error = read_inode(filesystem, node->inode_number, &inode);

	if (error != 0)
		return error;
	if ((inode.mode & SFS_MODE_TYPE) != SFS_MODE_REGULAR)
		return -SFS_EISDIR;
	if (size > SILLICONFS_MAX_FILE_SIZE)
		return -SFS_EOVERFLOW;
	if (size < inode.size) {
		uint32_t keep_blocks = (uint32_t)(size / SILLICONFS_BLOCK_SIZE +
		    (size % SILLICONFS_BLOCK_SIZE != 0));

		if (size % SILLICONFS_BLOCK_SIZE != 0) {
			uint32_t physical;
			error = map_block(filesystem, node->inode_number, &inode,
			    (uint32_t)(size / SILLICONFS_BLOCK_SIZE), false, &physical);
			if (error != 0)
				return error;
			if (physical != 0) {
				error = read_block(filesystem, physical, block);
				if (error != 0)
					return error;
				memset(block + size % SILLICONFS_BLOCK_SIZE, 0,
				    SILLICONFS_BLOCK_SIZE -
				    (size_t)(size % SILLICONFS_BLOCK_SIZE));
				error = write_block(filesystem, physical, block);
				if (error != 0)
					return error;
			}
		}
		for (uint32_t i = keep_blocks; i < SFS_DIRECT; i++) {
			if (inode.direct[i] != 0) {
				error = free_block(filesystem, inode.direct[i]);
				if (error != 0)
					return error;
				inode.direct[i] = 0;
			}
		}
		if (inode.indirect != 0) {
			uint8_t indirect[SILLICONFS_BLOCK_SIZE];
			bool any = false;

			error = read_block(filesystem, inode.indirect, indirect);
			if (error != 0)
				return error;
			for (uint32_t i = 0; i < SFS_INDIRECT_COUNT; i++) {
				uint32_t logical = SFS_DIRECT + i;
				uint32_t physical = get_le32(indirect + i * 4);

				if (physical != 0 && logical >= keep_blocks) {
					error = free_block(filesystem, physical);
					if (error != 0)
						return error;
					put_le32(indirect + i * 4, 0);
				} else if (physical != 0) {
					any = true;
				}
			}
			if (any) {
				error = write_block(filesystem, inode.indirect, indirect);
				if (error != 0)
					return error;
			} else {
				error = free_block(filesystem, inode.indirect);
				if (error != 0)
					return error;
				inode.indirect = 0;
			}
		}
	}
	inode.size = size;
	error = write_inode(filesystem, node->inode_number, &inode);
	if (error == 0)
		sync_vnode(node, &inode);
	return error;
}

static int
sfs_readdir(struct vnode *vnode, uint64_t index, struct vfs_dirent *entry)
{
	struct sfs_node *node = node_from_vnode(vnode);
	struct sfs_inode directory;
	uint8_t block[SILLICONFS_BLOCK_SIZE];
	uint64_t current = 0;
	int error;

	if (vnode->type != VNODE_DIRECTORY)
		return -SFS_ENOTDIR;
	if (entry == NULL)
		return -SFS_EINVAL;
	error = read_inode(node->filesystem, node->inode_number, &directory);
	if (error != 0)
		return error;
	for (uint32_t logical = 0; logical < directory.size / SILLICONFS_BLOCK_SIZE;
	    logical++) {
		uint32_t physical;
		error = read_logical_block(node->filesystem, &directory, logical, block,
		    &physical);
		if (error != 0 || physical == 0)
			return -SFS_EIO;
		for (uint32_t offset = 0; offset < SILLICONFS_BLOCK_SIZE;) {
			const uint8_t *record = block + offset;
			uint16_t length = get_le16(record + 4);
			uint8_t name_length = record[6];

			if (length < SFS_DIR_HEADER ||
			    length > SILLICONFS_BLOCK_SIZE - offset ||
			    length % 4 != 0 || name_length == 0 ||
			    length < ((SFS_DIR_HEADER + name_length + 3U) & ~3U))
				return -SFS_EIO;
			if (current++ == index) {
				struct sfs_inode target;
				uint32_t number = get_le32(record);

				error = read_inode(node->filesystem, number, &target);
				if (error != 0)
					return error;
				memset(entry, 0, sizeof(*entry));
				entry->inode = number;
				entry->size = target.size;
				entry->mode = target.mode & 07777U;
				entry->type = (target.mode & SFS_MODE_TYPE) ==
				    SFS_MODE_DIRECTORY ? VNODE_DIRECTORY : VNODE_REGULAR;
				memcpy(entry->name, record + SFS_DIR_HEADER, name_length);
				return 1;
			}
			offset += length;
		}
	}
	return 0;
}

int
silliconfs_mount(const struct silliconfs_device *device, struct vnode **root)
{
	struct silliconfs *filesystem;
	struct vnode *root_node;
	int error;

	if (device == NULL || root == NULL || device->block_count == 0 ||
	    device->read_block == NULL || device->write_block == NULL)
		return -SFS_EINVAL;
	*root = NULL;
	filesystem = kheap_alloc(sizeof(*filesystem));
	if (filesystem == NULL)
		return -SFS_ENOMEM;
	memset(filesystem, 0, sizeof(*filesystem));
	filesystem->device = *device;
	filesystem->super.total_blocks = device->block_count;
	error = load_superblock(filesystem);
	if (error == 0)
		error = validate_filesystem(filesystem);
	if (error != 0) {
		(void)kheap_free(filesystem);
		return error;
	}
	error = make_node(filesystem, SFS_ROOT_INODE, &root_node);
	if (error != 0) {
		(void)kheap_free(filesystem);
		return error;
	}
	*root = root_node;
	return 0;
}
