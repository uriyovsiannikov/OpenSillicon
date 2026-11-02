#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/procfs.h"
#include "kernel/timer.h"

#define PROCFS_EINVAL 22
#define PROCFS_ENOENT 2
#define PROCFS_ENOMEM 12
#define PROCFS_ENOTDIR 20
#define PROCFS_CONTENT_MAX 128U
#define PROCFS_TICK_HZ 100U

enum procfs_entry {
	PROCFS_ROOT,
	PROCFS_SELF,
	PROCFS_UPTIME
};

struct procfs_node {
	struct vnode vnode;
	enum procfs_entry entry;
	size_t content_length;
	uint64_t self_inode;
	uint64_t uptime_inode;
	char name[16];
	char content[PROCFS_CONTENT_MAX];
};

static int procfs_lookup(struct vnode *, const char *, size_t, struct vnode **);
static int procfs_read(struct vnode *, uint64_t, void *, size_t, size_t *);
static int procfs_readdir(struct vnode *, uint64_t, struct vfs_dirent *);
static void procfs_destroy(struct vnode *);

static const struct vnode_operations procfs_operations = {
	.lookup = procfs_lookup,
	.read = procfs_read,
	.readdir = procfs_readdir,
	.destroy = procfs_destroy
};

static struct procfs_node *
procfs_from_vnode(struct vnode *vnode)
{
	return (struct procfs_node *)vnode->private_data;
}

static bool
append_text(struct procfs_node *node, const char *text)
{
	size_t length = strlen(text);

	if (length > sizeof(node->content) - node->content_length)
		return false;
	memcpy(node->content + node->content_length, text, length);
	node->content_length += length;
	return true;
}

static bool
append_u64(struct procfs_node *node, uint64_t value)
{
	char digits[20];
	size_t length = 0;

	do {
		digits[length++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0);
	if (length > sizeof(node->content) - node->content_length)
		return false;
	while (length != 0)
		node->content[node->content_length++] = digits[--length];
	return true;
}

static struct procfs_node *
procfs_allocate(const char *name, enum procfs_entry entry,
	enum vnode_type type, uint64_t inode)
{
	struct procfs_node *node;

	if (inode == 0)
		inode = vfs_allocate_inode();
	if (inode == 0)
		return NULL;
	node = kheap_alloc(sizeof(*node));
	if (node == NULL)
		return NULL;
	memset(node, 0, sizeof(*node));
	memcpy(node->name, name, strlen(name) + 1U);
	node->entry = entry;
	node->vnode = (struct vnode){
		.operations = &procfs_operations,
		.type = type,
		.mode = type == VNODE_DIRECTORY ? 0555U : 0444U,
		.inode = inode,
		.references = 1,
		.private_data = node
	};
	return node;
}

static int
procfs_make_self(uint64_t inode, struct procfs_node **result)
{
	struct process_snapshot_entry current;
	struct procfs_node *node;
	static const char *const states[] = {
		[PROCESS_READY] = "ready",
		[PROCESS_RUNNING] = "running",
		[PROCESS_BLOCKED] = "blocked",
		[PROCESS_ZOMBIE] = "zombie"
	};
	int error;

	error = process_current_snapshot(&current);
	if (error != 0)
		return error;
	node = procfs_allocate("self", PROCFS_SELF, VNODE_REGULAR, inode);
	if (node == NULL)
		return -PROCFS_ENOMEM;
	if (!append_text(node, "Pid:\t") || !append_u64(node, current.pid) ||
	    !append_text(node, "\nPPid:\t") ||
	    !append_u64(node, current.parent_pid) ||
	    !append_text(node, "\nState:\t") ||
	    current.state >= sizeof(states) / sizeof(states[0]) ||
	    !append_text(node, states[current.state]) ||
	    !append_text(node, "\nExitStatus:\t") ||
	    !append_u64(node, current.exit_status) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -PROCFS_EINVAL;
	}
	node->content[node->content_length] = '\0';
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_make_uptime(uint64_t inode, struct procfs_node **result)
{
	struct procfs_node *node = procfs_allocate("uptime", PROCFS_UPTIME,
	    VNODE_REGULAR, inode);
	uint64_t ticks;
	uint64_t fractional;

	if (node == NULL)
		return -PROCFS_ENOMEM;
	ticks = timer_ticks();
	fractional = ticks % PROCFS_TICK_HZ;
	if (!append_u64(node, ticks / PROCFS_TICK_HZ) ||
	    !append_text(node, ".") ||
	    !append_text(node, (const char[]){
		(char)('0' + fractional / 10U),
		(char)('0' + fractional % 10U), '\0'
	    }) || !append_text(node, "\n")) {
		vnode_put(&node->vnode);
		return -PROCFS_EINVAL;
	}
	node->content[node->content_length] = '\0';
	node->vnode.size = node->content_length;
	*result = node;
	return 0;
}

static int
procfs_lookup(struct vnode *directory, const char *name, size_t name_length,
	struct vnode **result)
{
	struct procfs_node *node;
	int error;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -PROCFS_ENOTDIR;
	if (name == NULL || result == NULL)
		return -PROCFS_EINVAL;
	*result = NULL;
	node = procfs_from_vnode(directory);
	if (node->entry != PROCFS_ROOT)
		return -PROCFS_ENOTDIR;
	if (name_length == 4 && memcmp(name, "self", 4) == 0) {
		struct procfs_node *self;

		error = procfs_make_self(node->self_inode, &self);
		if (error == 0)
			*result = &self->vnode;
		return error;
	}
	if (name_length == 6 && memcmp(name, "uptime", 6) == 0) {
		struct procfs_node *uptime;

		error = procfs_make_uptime(node->uptime_inode, &uptime);
		if (error == 0)
			*result = &uptime->vnode;
		return error;
	}
	return -PROCFS_ENOENT;
}

static int
procfs_read(struct vnode *vnode, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct procfs_node *node;
	size_t available;

	if (vnode == NULL || read_length == NULL ||
	    (buffer == NULL && length != 0))
		return -PROCFS_EINVAL;
	*read_length = 0;
	if (vnode->type == VNODE_DIRECTORY)
		return -PROCFS_EINVAL;
	node = procfs_from_vnode(vnode);
	if (offset >= node->content_length)
		return 0;
	available = node->content_length - (size_t)offset;
	if (length > available)
		length = available;
	if (length != 0)
		memcpy(buffer, node->content + (size_t)offset, length);
	*read_length = length;
	return 0;
}

static int
procfs_readdir(struct vnode *directory, uint64_t index,
	struct vfs_dirent *entry)
{
	static const char *const names[] = { "self", "uptime" };
	struct procfs_node *node;

	if (directory == NULL || directory->type != VNODE_DIRECTORY)
		return -PROCFS_ENOTDIR;
	if (entry == NULL)
		return -PROCFS_EINVAL;
	if (index >= 2)
		return 0;
	node = procfs_from_vnode(directory);
	memset(entry, 0, sizeof(*entry));
	entry->inode = index == 0 ? node->self_inode : node->uptime_inode;
	entry->mode = 0444U;
	entry->type = VNODE_REGULAR;
	memcpy(entry->name, names[index], strlen(names[index]) + 1U);
	return 1;
}

static void
procfs_destroy(struct vnode *vnode)
{
	(void)kheap_free(procfs_from_vnode(vnode));
}

int
procfs_mount(const char *path)
{
	struct procfs_node *root = procfs_allocate("", PROCFS_ROOT,
	    VNODE_DIRECTORY, 0);
	int error;

	if (root == NULL)
		return -PROCFS_ENOMEM;
	root->self_inode = vfs_allocate_inode();
	root->uptime_inode = vfs_allocate_inode();
	if (root->self_inode == 0 || root->uptime_inode == 0) {
		vnode_put(&root->vnode);
		return -PROCFS_ENOMEM;
	}
	error = vfs_mount(path, &root->vnode);
	vnode_put(&root->vnode);
	return error;
}
