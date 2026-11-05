#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/devfs.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/random.h"
#include "kernel/tty.h"

#define DEVFS_EINVAL 22
#define DEVFS_ENOENT 2
#define DEVFS_ENOTDIR 20
#define DEVFS_ENOMEM 12
#define DEVFS_EIO 5

enum devfs_device {
	DEVFS_DIRECTORY,
	DEVFS_CONSOLE,
	DEVFS_TTY,
	DEVFS_NULL,
	DEVFS_ZERO,
	DEVFS_RANDOM,
	DEVFS_URANDOM
};

struct devfs_node {
	struct vnode vnode;
	struct devfs_node *children;
	struct devfs_node *next;
	enum devfs_device device;
	char name[VFS_NAME_MAX + 1];
};

static int devfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int devfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int devfs_write(struct vnode *, uint64_t, const void *, size_t, size_t *);
static int devfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void devfs_destroy(struct vnode *);

static const struct vnode_operations devfs_operations = {
	.lookup = devfs_lookup,
	.read = devfs_read,
	.write = devfs_write,
	.readdir = devfs_readdir,
	.destroy = devfs_destroy
};

static struct devfs_node *
devfs_from_vnode(struct vnode *vnode)
{
	return (struct devfs_node *)vnode->private_data;
}

static struct devfs_node *
devfs_allocate(const char *name, enum devfs_device device)
{
	struct devfs_node *node;
	uint64_t inode;
	enum vnode_type type = device == DEVFS_DIRECTORY ? VNODE_DIRECTORY :
	    VNODE_DEVICE;

	inode = vfs_allocate_inode();
	if (inode == 0)
		return NULL;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return NULL;
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, strlen(name) + 1U);
	node->device = device;
	node->vnode = (struct vnode){
		.operations = &devfs_operations,
		.type = type,
		.mode = device == DEVFS_DIRECTORY ? 0555U : 0666U,
		.inode = inode,
		.references = 1,
		.private_data = node
	};
	return node;
}

static void
devfs_destroy(struct vnode *vnode)
{
	struct devfs_node *node = devfs_from_vnode(vnode);
	struct devfs_node *child = node->children;

	while (child != NULL) {
		struct devfs_node *next = child->next;

		child->next = NULL;
		vnode_put(&child->vnode);
		child = next;
	}
	(void)kheap_free(node);
}

static int
devfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct devfs_node *parent;
	struct devfs_node *child;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -DEVFS_ENOTDIR;
	if (name == NULL || name_length == 0 || name_length > VFS_NAME_MAX ||
	    result == NULL)
		return -DEVFS_EINVAL;
	*result = NULL;
	parent = devfs_from_vnode(directory);
	for (child = parent->children; child != NULL; child = child->next) {
		if (strlen(child->name) == name_length &&
		    memcmp(child->name, name, name_length) == 0) {
			if (!vnode_get(&child->vnode))
				return -DEVFS_EINVAL;
			*result = &child->vnode;
			return 0;
		}
	}
	return -DEVFS_ENOENT;
}

static int
devfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct devfs_node *node;

	(void)offset;
	if (vnode == NULL || read_length == NULL ||
	    (buffer == NULL && length != 0))
		return -DEVFS_EINVAL;
	*read_length = 0;
	node = devfs_from_vnode(vnode);
	switch (node->device) {
	case DEVFS_CONSOLE:
	case DEVFS_TTY:
		return tty_read(buffer, length, read_length);
	case DEVFS_NULL:
		return 0;
	case DEVFS_ZERO:
		if (length != 0)
			memset(buffer, 0, length);
		*read_length = length;
		return 0;
	case DEVFS_RANDOM:
	case DEVFS_URANDOM:
		if (length > KERNEL_RANDOM_MAX_REQUEST)
			length = KERNEL_RANDOM_MAX_REQUEST;
		if (length == 0)
			return 0;
		if (!kernel_random_bytes(buffer, length))
			return -11;
		*read_length = length;
		return 0;
	case DEVFS_DIRECTORY:
		return -DEVFS_EINVAL;
	}
	return -DEVFS_EINVAL;
}

static int
devfs_write(struct vnode *vnode, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct devfs_node *node;

	(void)offset;
	if (vnode == NULL || write_length == NULL ||
	    (buffer == NULL && length != 0))
		return -DEVFS_EINVAL;
	*write_length = 0;
	node = devfs_from_vnode(vnode);
	switch (node->device) {
	case DEVFS_CONSOLE:
	case DEVFS_TTY:
		if (!tty_write(buffer, length) || !serial_write(buffer, length))
			return -DEVFS_EIO;
		*write_length = length;
		return 0;
	case DEVFS_NULL:
	case DEVFS_ZERO:
		*write_length = length;
		return 0;
	case DEVFS_RANDOM:
	case DEVFS_URANDOM:
		return -DEVFS_EINVAL;
	case DEVFS_DIRECTORY:
		return -DEVFS_EINVAL;
	}
	return -DEVFS_EINVAL;
}

static int
devfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	struct devfs_node *child;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -DEVFS_ENOTDIR;
	if (entry == NULL)
		return -DEVFS_EINVAL;
	child = devfs_from_vnode(directory)->children;
	while (child != NULL && index != 0) {
		child = child->next;
		index--;
	}
	if (child == NULL)
		return 0;
	memset(entry, 0, sizeof(*entry));
	entry->inode = child->vnode.inode;
	entry->mode = child->vnode.mode;
	entry->type = (uint32_t)child->vnode.type;
	memcpy(entry->name, child->name, strlen(child->name) + 1U);
	return 1;
}

static struct vnode *
devfs_create_root(void)
{
	static const struct {
		const char *name;
		enum devfs_device device;
	} devices[] = {
		{ "console", DEVFS_CONSOLE },
		{ "tty", DEVFS_TTY },
		{ "null", DEVFS_NULL },
		{ "zero", DEVFS_ZERO },
		{ "random", DEVFS_RANDOM },
		{ "urandom", DEVFS_URANDOM }
	};
	struct devfs_node *root;
	struct devfs_node **tail;
	size_t i;

	root = devfs_allocate("", DEVFS_DIRECTORY);
	if (root == NULL)
		return NULL;
	tail = &root->children;
	for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
		struct devfs_node *child = devfs_allocate(devices[i].name,
		    devices[i].device);

		if (child == NULL) {
			vnode_put(&root->vnode);
			return NULL;
		}
		*tail = child;
		tail = &child->next;
	}
	return &root->vnode;
}

int
devfs_mount(const char *path)
{
	struct vnode *root = devfs_create_root();
	int error;

	if (root == NULL)
		return -DEVFS_ENOMEM;
	error = vfs_mount(path, root);
	vnode_put(root);
	return error;
}
