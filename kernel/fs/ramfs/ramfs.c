#include <stdint.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/vfs.h"

#define RAMFS_EEXIST 17
#define RAMFS_EINVAL 22
#define RAMFS_EISDIR 21
#define RAMFS_ENAMETOOLONG 36
#define RAMFS_ENOENT 2
#define RAMFS_ENOMEM 12
#define RAMFS_ENOTDIR 20
#define RAMFS_EOVERFLOW 75

struct ramfs_node {
	struct vnode vnode;
	struct ramfs_node *children;
	struct ramfs_node *next;
	size_t capacity;
	uint8_t *data;
	char name[VFS_NAME_MAX + 1];
};

static int ramfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int ramfs_create(struct vnode *, const char *, size_t, enum vnode_type,
	uint32_t, struct vnode **);
static int ramfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int ramfs_write(struct vnode *, uint64_t, const void *, size_t, size_t *);
static int ramfs_truncate(struct vnode *, uint64_t);
static int ramfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void ramfs_destroy(struct vnode *);

static const struct vnode_operations ramfs_operations = {
	.lookup = ramfs_lookup,
	.create = ramfs_create,
	.read = ramfs_read,
	.write = ramfs_write,
	.truncate = ramfs_truncate,
	.readdir = ramfs_readdir,
	.destroy = ramfs_destroy
};

static struct ramfs_node *
ramfs_from_vnode(struct vnode *node)
{
	return (struct ramfs_node *)((uintptr_t)node -
	    offsetof(struct ramfs_node, vnode));
}

static struct ramfs_node *
ramfs_allocate(const char *name, size_t name_length, enum vnode_type type,
	uint32_t mode)
{
	struct ramfs_node *node;
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
	node->vnode = (struct vnode){
		.operations = &ramfs_operations,
		.type = type,
		.mode = mode & 07777U,
		.inode = inode,
		.references = 1,
		.private_data = node
	};
	return node;
}

struct vnode *
ramfs_create_root(void)
{
	struct ramfs_node *root = ramfs_allocate("", 0, VNODE_DIRECTORY, 0755);

	return root == NULL ? NULL : &root->vnode;
}

static int
ramfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct ramfs_node *parent;
	struct ramfs_node *child;

	if (directory->type != VNODE_DIRECTORY)
		return -RAMFS_ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL)
		return -RAMFS_EINVAL;
	parent = ramfs_from_vnode(directory);
	for (child = parent->children; child != NULL; child = child->next) {
		if (strlen(child->name) == name_length &&
		    memcmp(child->name, name, name_length) == 0) {
			if (!vnode_get(&child->vnode))
				return -RAMFS_EINVAL;
			*result = &child->vnode;
			return 0;
		}
	}
	return -RAMFS_ENOENT;
}

static int
ramfs_create(struct vnode *directory, const char *name, size_t name_length,
	enum vnode_type type, uint32_t mode, struct vnode **result)
{
	struct ramfs_node *parent;
	struct ramfs_node *child;
	struct ramfs_node *cursor;

	if (directory->type != VNODE_DIRECTORY)
		return -RAMFS_ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL || (type != VNODE_REGULAR && type != VNODE_DIRECTORY))
		return -RAMFS_EINVAL;
	for (size_t i = 0; i < name_length; i++) {
		if (name[i] == '/')
			return -RAMFS_EINVAL;
	}
	parent = ramfs_from_vnode(directory);
	for (cursor = parent->children; cursor != NULL; cursor = cursor->next) {
		if (strlen(cursor->name) == name_length &&
		    memcmp(cursor->name, name, name_length) == 0)
			return -RAMFS_EEXIST;
	}
	child = ramfs_allocate(name, name_length, type, mode);
	if (child == NULL)
		return -RAMFS_ENOMEM;
	child->next = parent->children;
	parent->children = child;
	if (!vnode_get(&child->vnode)) {
		parent->children = child->next;
		(void)kheap_free(child);
		return -RAMFS_EINVAL;
	}
	*result = &child->vnode;
	return 0;
}

static int
ramfs_read(struct vnode *node, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct ramfs_node *file;
	size_t amount;

	if (node->type != VNODE_REGULAR)
		return -RAMFS_EISDIR;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -RAMFS_EINVAL;
	file = ramfs_from_vnode(node);
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
ramfs_resize(struct ramfs_node *file, uint64_t size)
{
	uint8_t *data;
	size_t new_capacity;

	if (size > SIZE_MAX)
		return -RAMFS_EOVERFLOW;
	if ((size_t)size > file->capacity) {
		new_capacity = file->capacity == 0 ? 64 : file->capacity;
		while (new_capacity < size) {
			if (new_capacity > SIZE_MAX / 2) {
				new_capacity = (size_t)size;
				break;
			}
			new_capacity *= 2;
		}
		data = kheap_alloc(new_capacity);
		if (data == NULL)
			return -RAMFS_ENOMEM;
		memset(data, 0, new_capacity);
		if (file->vnode.size != 0)
			memcpy(data, file->data, (size_t)file->vnode.size);
		if (file->data != NULL)
			(void)kheap_free(file->data);
		file->data = data;
		file->capacity = new_capacity;
	}
	if (size > file->vnode.size)
		memset(file->data + file->vnode.size, 0,
		    (size_t)(size - file->vnode.size));
	file->vnode.size = size;
	return 0;
}

static int
ramfs_write(struct vnode *node, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct ramfs_node *file;
	uint64_t end;
	int error;

	if (node->type != VNODE_REGULAR)
		return -RAMFS_EISDIR;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -RAMFS_EINVAL;
	if (length > UINT64_MAX - offset)
		return -RAMFS_EOVERFLOW;
	if (length == 0) {
		*write_length = 0;
		return 0;
	}
	end = offset + length;
	file = ramfs_from_vnode(node);
	error = ramfs_resize(file, end > node->size ? end : node->size);
	if (error != 0)
		return error;
	memcpy(file->data + (size_t)offset, buffer, length);
	*write_length = length;
	return 0;
}

static int
ramfs_truncate(struct vnode *node, uint64_t size)
{
	if (node->type != VNODE_REGULAR)
		return -RAMFS_EISDIR;
	return ramfs_resize(ramfs_from_vnode(node), size);
}

static int
ramfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	struct ramfs_node *child;

	if (directory->type != VNODE_DIRECTORY)
		return -RAMFS_ENOTDIR;
	if (entry == NULL)
		return -RAMFS_EINVAL;
	child = ramfs_from_vnode(directory)->children;
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
ramfs_destroy(struct vnode *node)
{
	struct ramfs_node *ramnode = ramfs_from_vnode(node);
	struct ramfs_node *child = ramnode->children;

	while (child != NULL) {
		struct ramfs_node *next = child->next;

		child->next = NULL;
		vnode_put(&child->vnode);
		child = next;
	}
	if (ramnode->data != NULL)
		(void)kheap_free(ramnode->data);
	(void)kheap_free(ramnode);
}
