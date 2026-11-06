#include <stdint.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/vfs.h"

#define INITRD_HEADER_SIZE 110U
#define INITRD_MAX_ENTRIES 4096U
#define INITRD_MAX_NODES 8192U
#define INITRD_EINVAL 22
#define INITRD_EIO 5
#define INITRD_EISDIR 21
#define INITRD_ENAMETOOLONG 36
#define INITRD_ENOENT 2
#define INITRD_ENOMEM 12
#define INITRD_ENOTDIR 20
#define INITRD_EROFS 30

#define INITRD_TYPE_MASK 0170000U
#define INITRD_TYPE_REGULAR 0100000U
#define INITRD_TYPE_DIRECTORY 0040000U

struct initramfs_node {
	struct vnode vnode;
	struct initramfs_node *children;
	struct initramfs_node *next;
	const uint8_t *data;
	char name[VFS_NAME_MAX + 1];
	bool explicit_entry;
};

static int initramfs_lookup(struct vnode *, const char *, size_t,
	struct vnode **);
static int initramfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int initramfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void initramfs_destroy(struct vnode *);

static const struct vnode_operations initramfs_operations = {
	.lookup = initramfs_lookup,
	.read = initramfs_read,
	.readdir = initramfs_readdir,
	.destroy = initramfs_destroy
};

static struct initramfs_node *
initramfs_from_vnode(struct vnode *node)
{
	return (struct initramfs_node *)((uintptr_t)node -
	    offsetof(struct initramfs_node, vnode));
}

static int
parse_hex32(const uint8_t *bytes, uint32_t *value)
{
	uint32_t result = 0;
	unsigned int i;

	for (i = 0; i < 8; i++) {
		uint8_t c = bytes[i];
		uint32_t digit;

		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return -INITRD_EINVAL;
		result = (result << 4) | digit;
	}
	*value = result;
	return 0;
}

static bool
align4(size_t value, size_t *aligned)
{
	if (value > SIZE_MAX - 3)
		return false;
	*aligned = (value + 3) & ~(size_t)3;
	return true;
}

static struct initramfs_node *
find_child(struct initramfs_node *directory, const char *name, size_t length)
{
	struct initramfs_node *node;

	for (node = directory->children; node != NULL; node = node->next) {
		if (strlen(node->name) == length &&
		    memcmp(node->name, name, length) == 0)
			return node;
	}
	return NULL;
}

static struct initramfs_node *
allocate_node(const char *name, size_t name_length, enum vnode_type type,
	uint32_t mode, uint32_t uid, uint32_t gid, bool explicit_entry)
{
	struct initramfs_node *node;
	uint64_t inode;

	if (name_length > VFS_NAME_MAX)
		return NULL;
	inode = vfs_allocate_inode();
	if (inode == 0)
		return NULL;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return NULL;
	memset(node, 0, sizeof(*node));
	if (name_length != 0)
		memcpy(node->name, name, name_length);
	node->name[name_length] = '\0';
	node->explicit_entry = explicit_entry;
	node->vnode = (struct vnode){
		.operations = &initramfs_operations,
		.type = type,
		.mode = mode & 07777U,
		.uid = uid,
		.gid = gid,
		.inode = inode,
		.references = 1,
		.private_data = node
	};
	return node;
}

static int
insert_entry(struct initramfs_node *root, const char *path, size_t path_length,
	enum vnode_type type, uint32_t mode, const uint8_t *data, uint64_t size,
	uint32_t uid, uint32_t gid, uint64_t *node_count)
{
	struct initramfs_node *directory = root;
	size_t position = 0;

	while (path_length >= 2 && path[0] == '.' && path[1] == '/') {
		path += 2;
		path_length -= 2;
	}
	if (((path_length == 0) ||
	    (path_length == 1 && path[0] == '.')) &&
	    type == VNODE_DIRECTORY) {
		root->vnode.mode = mode & 07777U;
		root->vnode.uid = uid;
		root->vnode.gid = gid;
		return 0;
	}
	if (path_length != 0 && path[path_length - 1] == '/') {
		if (type != VNODE_DIRECTORY)
			return -INITRD_EINVAL;
		path_length--;
	}

	if (path_length == 0 || path_length >= VFS_PATH_MAX || path[0] == '/')
		return -INITRD_EINVAL;
	while (position < path_length) {
		size_t begin = position;
		size_t length;
		bool last;
		struct initramfs_node *child;

		while (position < path_length && path[position] != '/')
			position++;
		length = position - begin;
		last = position == path_length;
		if (length == 0 || length > VFS_NAME_MAX ||
		    (length == 1 && path[begin] == '.') ||
		    (length == 2 && path[begin] == '.' && path[begin + 1] == '.'))
			return -INITRD_EINVAL;
		if (!last) {
			if (++position == path_length || path[position] == '/')
				return -INITRD_EINVAL;
			child = find_child(directory, path + begin, length);
			if (child == NULL) {
				if (*node_count == INITRD_MAX_NODES)
					return -INITRD_EINVAL;
				child = allocate_node(path + begin, length,
				    VNODE_DIRECTORY, 0755, uid, gid, false);
				if (child == NULL)
					return -INITRD_ENOMEM;
				(*node_count)++;
				child->next = directory->children;
				directory->children = child;
			}
			if (child->vnode.type != VNODE_DIRECTORY)
				return -INITRD_ENOTDIR;
			directory = child;
			continue;
		}
		child = find_child(directory, path + begin, length);
		if (child != NULL) {
			if (type != VNODE_DIRECTORY || child->explicit_entry ||
			    child->vnode.type != VNODE_DIRECTORY)
				return -INITRD_EINVAL;
			child->explicit_entry = true;
			child->vnode.mode = mode & 07777U;
			child->vnode.uid = uid;
			child->vnode.gid = gid;
			return 0;
		}
		if (*node_count == INITRD_MAX_NODES)
			return -INITRD_EINVAL;
		child = allocate_node(path + begin, length, type, mode, uid, gid, true);
		if (child == NULL)
			return -INITRD_ENOMEM;
		(*node_count)++;
		child->data = data;
		child->vnode.size = size;
		child->next = directory->children;
		directory->children = child;
		return 0;
	}
	return -INITRD_EINVAL;
}

int
initramfs_mount(const void *archive, size_t archive_size, struct vnode **root_out)
{
	const uint8_t *bytes = archive;
	struct initramfs_node *root;
	size_t offset = 0;
	uint64_t entries = 0;
	uint64_t node_count = 1;
	bool trailer_found = false;

	if (bytes == NULL || archive_size < INITRD_HEADER_SIZE || root_out == NULL)
		return -INITRD_EINVAL;
	*root_out = NULL;
	root = allocate_node("", 0, VNODE_DIRECTORY, 0755, 0, 0, true);
	if (root == NULL)
		return -INITRD_ENOMEM;
	while (offset <= archive_size &&
	    archive_size - offset >= INITRD_HEADER_SIZE) {
		uint32_t fields[13];
		uint32_t mode;
		uint32_t file_size;
		uint32_t name_size;
		uint32_t check;
		size_t name_offset;
		size_t data_offset;
		size_t data_end;
		size_t next_offset;
		const uint8_t *name;
		enum vnode_type type;
		int error;

		unsigned int field;

		if (memcmp(bytes + offset, "070701", 6) != 0 ||
		    offset + INITRD_HEADER_SIZE > archive_size)
			goto invalid;
		for (field = 0; field < 13; field++) {
			if (parse_hex32(bytes + offset + 6 + field * 8,
			    &fields[field]) != 0)
				goto invalid;
		}
		mode = fields[1];
		uint32_t uid = fields[2];
		uint32_t gid = fields[3];
		file_size = fields[6];
		if (fields[4] == 0)
			goto invalid;
		name_size = fields[11];
		check = fields[12];
		if (check != 0 || name_size < 1 || name_size > VFS_PATH_MAX)
			goto invalid;
		name_offset = offset + INITRD_HEADER_SIZE;
		if (name_offset > archive_size ||
		    name_size > archive_size - name_offset ||
		    !align4(name_offset + name_size, &data_offset) ||
		    data_offset > archive_size ||
		    file_size > archive_size - data_offset)
			goto invalid;
		name = bytes + name_offset;
		if (name[name_size - 1] != '\0' ||
		    memchr(name, '\0', name_size - 1) != NULL)
			goto invalid;
		if (!align4(data_offset + file_size, &data_end) ||
		    data_end > archive_size)
			goto invalid;
		next_offset = data_end;
		if (name_size == sizeof("TRAILER!!!") &&
		    memcmp(name, "TRAILER!!!", sizeof("TRAILER!!!")) == 0) {
			if (file_size != 0)
				goto invalid;
			trailer_found = true;
			offset = next_offset;
			break;
		}
		if (++entries > INITRD_MAX_ENTRIES)
			goto invalid;
		switch (mode & INITRD_TYPE_MASK) {
		case INITRD_TYPE_REGULAR:
			if (fields[4] != 1)
				goto invalid;
			type = VNODE_REGULAR;
			break;
		case INITRD_TYPE_DIRECTORY:
			if (file_size != 0)
				goto invalid;
			type = VNODE_DIRECTORY;
			break;
		default:
			goto invalid;
		}
		error = insert_entry(root, (const char *)name, name_size - 1, type,
		    mode, bytes + data_offset, file_size, uid, gid, &node_count);
		if (error != 0)
			goto invalid;
		offset = next_offset;
	}
	if (!trailer_found)
		goto invalid;
	while (offset < archive_size) {
		if (bytes[offset++] != 0)
			goto invalid;
	}
	*root_out = &root->vnode;
	return 0;

invalid:
	vnode_put(&root->vnode);
	return -INITRD_EIO;
}

static int
initramfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct initramfs_node *child;

	if (directory->type != VNODE_DIRECTORY)
		return -INITRD_ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL)
		return -INITRD_EINVAL;
	for (child = initramfs_from_vnode(directory)->children; child != NULL;
	    child = child->next) {
		if (strlen(child->name) == name_length &&
		    memcmp(child->name, name, name_length) == 0) {
			if (!vnode_get(&child->vnode))
				return -INITRD_EINVAL;
			*result = &child->vnode;
			return 0;
		}
	}
	return -INITRD_ENOENT;
}

static int
initramfs_read(struct vnode *node, uint64_t offset, void *buffer,
	size_t length, size_t *read_length)
{
	struct initramfs_node *file;
	size_t amount;

	if (node->type != VNODE_REGULAR)
		return -INITRD_EISDIR;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -INITRD_EINVAL;
	file = initramfs_from_vnode(node);
	if (offset >= node->size || length == 0) {
		*read_length = 0;
		return 0;
	}
	amount = node->size - offset < length ? (size_t)(node->size - offset) :
	    length;
	memcpy(buffer, file->data + (size_t)offset, amount);
	*read_length = amount;
	return 0;
}

static int
initramfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	struct initramfs_node *child;

	if (directory->type != VNODE_DIRECTORY)
		return -INITRD_ENOTDIR;
	if (entry == NULL)
		return -INITRD_EINVAL;
	child = initramfs_from_vnode(directory)->children;
	while (child != NULL && index != 0) {
		child = child->next;
		index--;
	}
	if (child == NULL)
		return 0;
	memset(entry, 0, sizeof(*entry));
	entry->inode = child->vnode.inode;
	entry->size = child->vnode.size;
	entry->mode = child->vnode.mode;
	entry->type = (uint32_t)child->vnode.type;
	memcpy(entry->name, child->name, strlen(child->name) + 1);
	return 1;
}

static void
initramfs_destroy(struct vnode *node)
{
	struct initramfs_node *ramnode = initramfs_from_vnode(node);
	struct initramfs_node *child = ramnode->children;

	while (child != NULL) {
		struct initramfs_node *next = child->next;

		child->next = NULL;
		vnode_put(&child->vnode);
		child = next;
	}
	(void)kheap_free(ramnode);
}
