#include <stdint.h>
#include <string.h>

#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/vfs.h"

#define VFS_EBADF 9
#define VFS_EBUSY 16
#define VFS_EEXIST 17
#define VFS_EIO 5
#define VFS_EOVERFLOW 75
#define VFS_EINVAL 22
#define VFS_EISDIR 21
#define VFS_ENAMETOOLONG 36
#define VFS_ENOENT 2
#define VFS_ENOMEM 12
#define VFS_ENOTDIR 20
#define VFS_ESPIPE 29
#define VFS_EROFS 30

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

struct vfs_mount {
	char path[VFS_PATH_MAX];
	size_t path_length;
	struct vnode *root;
	struct vfs_mount *next;
};

struct vfs_file_context {
	struct vnode *node;
	uint32_t flags;
	char path[VFS_PATH_MAX];
};

static struct vfs_mount *mounts;
static uint64_t next_inode = 1;
static bool initialized;

static bool
path_has_prefix(const char *path, const char *prefix, size_t length)
{
	size_t i;

	for (i = 0; i < length; i++) {
		if (path[i] == '\0' || path[i] != prefix[i])
			return false;
	}
	return true;
}

static int
canonicalize_path(const char *path, char output[VFS_PATH_MAX])
{
	size_t input = 1;
	size_t position = 1;

	if (path == NULL || path[0] != '/')
		return -VFS_EINVAL;
	output[0] = '/';
	for (input = 1; path[input] != '\0';) {
		size_t begin;
		size_t length;

		while (path[input] == '/')
			input++;
		if (path[input] == '\0')
			break;
		begin = input;
		while (path[input] != '\0' && path[input] != '/')
			input++;
		length = input - begin;
		if (length == 1 && path[begin] == '.')
			continue;
		if (length == 2 && path[begin] == '.' && path[begin + 1] == '.') {
			while (position > 1 && output[position - 1] != '/')
				position--;
			if (position > 1)
				position--;
			continue;
		}
		if (length > VFS_NAME_MAX || position + (position > 1) + length >=
		    VFS_PATH_MAX)
			return -VFS_ENAMETOOLONG;
		if (position > 1)
			output[position++] = '/';
		for (size_t i = 0; i < length; i++)
			output[position++] = path[begin + i];
	}
	output[position] = '\0';
	return 0;
}

bool
vfs_init(void)
{
	if (initialized)
		return false;
	mounts = NULL;
	next_inode = 1;
	initialized = true;
	return true;
}

bool
vnode_get(struct vnode *node)
{
	if (node == NULL || node->references == 0 ||
	    node->references == UINT32_MAX)
		return false;
	node->references++;
	return true;
}

void
vnode_put(struct vnode *node)
{
	if (node == NULL || node->references == 0)
		return;
	node->references--;
	if (node->references != 0)
		return;
	if (node->operations->destroy != NULL)
		node->operations->destroy(node);
}

static struct vfs_mount *
find_mount(const char *path)
{
	struct vfs_mount *mount;
	struct vfs_mount *best = NULL;

	for (mount = mounts; mount != NULL; mount = mount->next) {
		if (mount->path_length > (best == NULL ? 0 : best->path_length) &&
		    path_has_prefix(path, mount->path, mount->path_length) &&
		    (mount->path_length == 1 || path[mount->path_length] == '\0' ||
		    path[mount->path_length] == '/'))
			best = mount;
	}
	return best;
}

uint64_t
vfs_allocate_inode(void)
{
	if (next_inode == UINT64_MAX)
		return 0;
	return next_inode++;
}

int
vfs_mount(const char *path, struct vnode *root)
{
	struct vfs_mount *mount;
	struct vfs_mount *existing;
	struct vnode *mountpoint = NULL;
	char canonical[VFS_PATH_MAX];
	int error;

	if (!initialized || root == NULL || root->type != VNODE_DIRECTORY)
		return -VFS_EINVAL;
	error = canonicalize_path(path, canonical);
	if (error != 0)
		return error;
	for (existing = mounts; existing != NULL; existing = existing->next) {
		if (existing->path_length == strlen(canonical) &&
		    memcmp(existing->path, canonical, existing->path_length) == 0)
			return -VFS_EBUSY;
	}
	if (strcmp(canonical, "/") != 0) {
		if (find_mount("/") == NULL)
			return -VFS_EINVAL;
		error = vfs_lookup(canonical, &mountpoint);
		if (error != 0)
			return error;
		if (mountpoint->type != VNODE_DIRECTORY) {
			vnode_put(mountpoint);
			return -VFS_ENOTDIR;
		}
	}
	mount = kheap_alloc(sizeof(*mount));
	if (mount == NULL) {
		vnode_put(mountpoint);
		return -VFS_ENOMEM;
	}
	if (!vnode_get(root)) {
		(void)kheap_free(mount);
		vnode_put(mountpoint);
		return -VFS_EINVAL;
	}
	memcpy(mount->path, canonical, strlen(canonical) + 1);
	mount->path_length = strlen(canonical);
	mount->root = root;
	mount->next = mounts;
	mounts = mount;
	vnode_put(mountpoint);
	return 0;
}

int
vfs_replace_root(struct vnode *root)
{
	struct vfs_mount *mount;
	struct vnode *old;

	if (!initialized || root == NULL || root->type != VNODE_DIRECTORY)
		return -VFS_EINVAL;
	for (mount = mounts; mount != NULL; mount = mount->next)
		if (mount->path_length == 1 && mount->path[0] == '/')
			break;
	if (mount == NULL || !vnode_get(root))
		return -VFS_EINVAL;
	old = mount->root;
	mount->root = root;
	vnode_put(old);
	return 0;
}

int
vfs_lookup(const char *path, struct vnode **result)
{
	char canonical[VFS_PATH_MAX];
	struct vfs_mount *mount;
	struct vnode *current;
	const char *relative;
	size_t position = 0;
	int error;

	if (!initialized || result == NULL)
		return -VFS_EINVAL;
	*result = NULL;
	error = canonicalize_path(path, canonical);
	if (error != 0)
		return error;
	mount = find_mount(canonical);
	if (mount == NULL)
		return -VFS_ENOENT;
	if (!vnode_get(mount->root))
		return -VFS_EINVAL;
	current = mount->root;
	relative = mount->path_length == 1 ? canonical + 1 :
	    canonical + mount->path_length;
	if (*relative == '/')
		relative++;
	while (relative[position] != '\0') {
		size_t begin = position;
		size_t length;
		struct vnode *next = NULL;

		while (relative[position] != '\0' && relative[position] != '/')
			position++;
		length = position - begin;
		while (relative[position] == '/')
			position++;
		if (current->type != VNODE_DIRECTORY ||
		    current->operations->lookup == NULL) {
			vnode_put(current);
			return -VFS_ENOTDIR;
		}
		error = current->operations->lookup(current, relative + begin,
		    length, &next);
		vnode_put(current);
		if (error != 0)
			return error;
		if (next == NULL)
			return -VFS_EIO;
		current = next;
	}
	*result = current;
	return 0;
}

int
vfs_lookup_at(const char *base_path, const char *path, struct vnode **result)
{
	char base[VFS_PATH_MAX];
	char combined[VFS_PATH_MAX];
	char canonical[VFS_PATH_MAX];
	size_t base_length;
	size_t path_length;
	int error;

	if (path == NULL || result == NULL)
		return -VFS_EINVAL;
	if (path[0] == '/')
		return vfs_lookup(path, result);
	if (path[0] == '\0')
		return -VFS_ENOENT;
	if (base_path == NULL)
		return -VFS_EINVAL;
	error = canonicalize_path(base_path, base);
	if (error != 0)
		return error;
	base_length = strlen(base);
	path_length = strlen(path);
	if (base_length + (base_length > 1U ? 1U : 0U) + path_length >=
	    sizeof(combined))
		return -VFS_ENAMETOOLONG;
	memcpy(combined, base, base_length);
	if (base_length > 1U)
		combined[base_length++] = '/';
	memcpy(combined + base_length, path, path_length + 1U);
	error = canonicalize_path(combined, canonical);
	if (error != 0)
		return error;
	return vfs_lookup(canonical, result);
}

int
vfs_create_path(const char *path, enum vnode_type type, uint32_t mode,
	struct vnode **result)
{
	char canonical[VFS_PATH_MAX];
	char parent_path[VFS_PATH_MAX];
	struct vnode *parent;
	bool parent_is_directory;
	const char *name;
	char *slash;
	int error;

	if (!initialized || result == NULL ||
	    (type != VNODE_REGULAR && type != VNODE_DIRECTORY))
		return -VFS_EINVAL;
	*result = NULL;
	error = canonicalize_path(path, canonical);
	if (error != 0)
		return error;
	if (strcmp(canonical, "/") == 0)
		return -VFS_EEXIST;
	slash = strrchr(canonical, '/');
	name = slash + 1;
	if (slash == canonical) {
		parent_path[0] = '/';
		parent_path[1] = '\0';
	} else {
		size_t parent_length = (size_t)(slash - canonical);
		memcpy(parent_path, canonical, parent_length);
		parent_path[parent_length] = '\0';
	}
	error = vfs_lookup(parent_path, &parent);
	if (error != 0)
		return error;
	if (parent->type != VNODE_DIRECTORY || parent->operations->create == NULL) {
		parent_is_directory = parent->type == VNODE_DIRECTORY;
		vnode_put(parent);
		return parent_is_directory ? -VFS_EROFS : -VFS_ENOTDIR;
	}
	error = parent->operations->create(parent, name, strlen(name), type, mode,
	    result);
	vnode_put(parent);
	return error;
}

int
vfs_stat_path(const char *path, struct vfs_stat *status)
{
	struct vnode *node;
	int error;

	if (status == NULL)
		return -VFS_EINVAL;
	error = vfs_lookup(path, &node);
	if (error != 0)
		return error;
	*status = (struct vfs_stat){
		.size = node->size,
		.mode = node->mode,
		.type = (uint32_t)node->type
	};
	vnode_put(node);
	return 0;
}

static int
file_read(void *context, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct vfs_file_context *file = context;

	if ((file->flags & VFS_O_ACCMODE) == VFS_O_WRONLY)
		return -VFS_EBADF;
	if (file->node->type == VNODE_DIRECTORY)
		return -VFS_EISDIR;
	if (file->node->operations->read == NULL)
		return -VFS_EBADF;
	return file->node->operations->read(file->node, offset, buffer, length,
	    read_length);
}

static int
file_write(void *context, uint64_t offset, const void *buffer, size_t length,
	size_t *write_length)
{
	struct vfs_file_context *file = context;

	if ((file->flags & VFS_O_ACCMODE) == VFS_O_RDONLY)
		return -VFS_EBADF;
	if (file->node->type == VNODE_DIRECTORY)
		return -VFS_EISDIR;
	if (file->node->operations->write == NULL)
		return -VFS_EROFS;
	return file->node->operations->write(file->node, offset, buffer, length,
	    write_length);
}

static int
file_readdir(void *context, uint64_t index, struct vfs_dirent *entry)
{
	struct vfs_file_context *file = context;

	if (file->node->type != VNODE_DIRECTORY)
		return -VFS_ENOTDIR;
	if ((file->flags & VFS_O_ACCMODE) != VFS_O_RDONLY)
		return -VFS_EBADF;
	if (file->node->operations->readdir == NULL)
		return -VFS_EBADF;
	return file->node->operations->readdir(file->node, index, entry);
}

static int
file_seek(void *context, uint64_t current, int64_t offset, int whence,
	uint64_t *result)
{
	struct vfs_file_context *file = context;
	uint64_t base;
	uint64_t target;

	if (file->node->type != VNODE_REGULAR)
		return -VFS_ESPIPE;
	switch (whence) {
	case VFS_SEEK_SET:
		base = 0;
		break;
	case VFS_SEEK_CUR:
		base = current;
		break;
	case VFS_SEEK_END:
		base = file->node->size;
		break;
	default:
		return -VFS_EINVAL;
	}
	if (offset < 0) {
		uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1U;
		if (magnitude > base)
			return -VFS_EINVAL;
		target = base - magnitude;
	} else {
		if ((uint64_t)offset > UINT64_MAX - base)
			return -VFS_EOVERFLOW;
		target = base + (uint64_t)offset;
	}
	if (target > INT64_MAX)
		return -VFS_EOVERFLOW;
	*result = target;
	return 0;
}

static int
file_stat(void *context, struct fd_metadata *metadata)
{
	struct vfs_file_context *file = context;
	struct vnode *node = file->node;

	if (node->type != VNODE_REGULAR && node->type != VNODE_DIRECTORY &&
	    node->type != VNODE_DEVICE)
		return -VFS_EINVAL;
	*metadata = (struct fd_metadata){
		.size = node->size,
		.inode = node->inode,
		.mode = node->mode,
		.type = (uint32_t)node->type
	};
	return 0;
}

static int
file_get_path(void *context, char *path, size_t capacity)
{
	struct vfs_file_context *file = context;
	size_t length;

	if (file->node->type != VNODE_DIRECTORY)
		return -VFS_ENOTDIR;
	length = strlen(file->path) + 1U;
	if (length > capacity)
		return -VFS_ENAMETOOLONG;
	memcpy(path, file->path, length);
	return 0;
}

static void
file_close(void *context)
{
	struct vfs_file_context *file = context;

	vnode_put(file->node);
	(void)kheap_free(file);
}

static const struct fd_operations vfs_file_operations = {
	.read = file_read,
	.write = file_write,
	.readdir = file_readdir,
	.seek = file_seek,
	.stat = file_stat,
	.get_path = file_get_path,
	.close = file_close
};

int
vfs_open(const char *path, uint32_t flags, uint32_t mode,
	struct fd_object **result)
{
	const uint32_t allowed = VFS_O_ACCMODE | VFS_O_CREAT | VFS_O_TRUNC |
	    VFS_O_DIRECTORY | VFS_O_NONBLOCK;
	struct vfs_file_context *context;
	struct vnode *node = NULL;
	struct fd_object *file;
	char canonical[VFS_PATH_MAX];
	int error;

	if (result == NULL || (flags & ~allowed) != 0 ||
	    (flags & VFS_O_ACCMODE) == VFS_O_ACCMODE ||
	    ((flags & (VFS_O_CREAT | VFS_O_DIRECTORY)) ==
	    (VFS_O_CREAT | VFS_O_DIRECTORY)) ||
	    ((flags & VFS_O_TRUNC) != 0 &&
	    (flags & VFS_O_ACCMODE) == VFS_O_RDONLY))
		return -VFS_EINVAL;
	*result = NULL;
	error = canonicalize_path(path, canonical);
	if (error != 0)
		return error;
	error = vfs_lookup(canonical, &node);
	if (error == -VFS_ENOENT && (flags & VFS_O_CREAT) != 0)
		error = vfs_create_path(path, VNODE_REGULAR, mode, &node);
	if (error != 0)
		return error;
	if (node->type == VNODE_DIRECTORY &&
	    (flags & VFS_O_DIRECTORY) == 0 &&
	    (flags & VFS_O_ACCMODE) != VFS_O_RDONLY) {
		vnode_put(node);
		return -VFS_EISDIR;
	}
	if ((flags & VFS_O_DIRECTORY) != 0 && node->type != VNODE_DIRECTORY) {
		vnode_put(node);
		return -VFS_ENOTDIR;
	}
	if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY &&
	    node->operations->write == NULL) {
		vnode_put(node);
		return -VFS_EROFS;
	}
	if ((flags & VFS_O_TRUNC) != 0) {
		if (node->type == VNODE_DIRECTORY || node->operations->truncate == NULL) {
			bool is_directory = node->type == VNODE_DIRECTORY;

			vnode_put(node);
			return is_directory ? -VFS_EISDIR : -VFS_EROFS;
		}
		error = node->operations->truncate(node, 0);
		if (error != 0) {
			vnode_put(node);
			return error;
		}
	}
	context = kheap_alloc(sizeof(*context));
	if (context == NULL) {
		vnode_put(node);
		return -VFS_ENOMEM;
	}
	context->node = node;
	context->flags = flags;
	memcpy(context->path, canonical, strlen(canonical) + 1U);
	file = fd_object_create(&vfs_file_operations, context);
	if (file == NULL) {
		vnode_put(node);
		(void)kheap_free(context);
		return -VFS_ENOMEM;
	}
	file->status_flags = flags & (VFS_O_ACCMODE | VFS_O_NONBLOCK);
	*result = file;
	return 0;
}
