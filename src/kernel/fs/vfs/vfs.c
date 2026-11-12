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
#define VFS_EACCES 13
#define VFS_EPERM 1
#define VFS_EISDIR 21
#define VFS_ENAMETOOLONG 36
#define VFS_ENOENT 2
#define VFS_ENOMEM 12
#define VFS_ENXIO 6
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
canonicalize_path(const char *path, char output[VFS_PATH_MAX],
	uint32_t uid, uint32_t gid)
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
		if ((length == 1 && path[begin] == '.') ||
		    (length == 2 && path[begin] == '.' && path[begin + 1] == '.')) {
			struct vnode *directory = NULL;
			int error;

			/* Validate the component before '.' or '..' erases it. The
			 * accumulated prefix contains no dot components, so this lookup
			 * cannot recurse through this check again. */
			output[position] = '\0';
			error = vfs_lookup_as(output, uid, gid, &directory);
			if (error != 0)
				return error;
			if (directory->type != VNODE_DIRECTORY)
				error = -VFS_ENOTDIR;
			else if (!vfs_access(directory, uid, gid, VFS_ACCESS_EXEC))
				error = -VFS_EACCES;
			vnode_put(directory);
			if (error != 0)
				return error;
			if (length == 1)
				continue;
		}
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
	/* Keep the last slash until the operation can enforce directory type. */
	if (position > 1 && path[input - 1] == '/') {
		if (position + 1 >= VFS_PATH_MAX)
			return -VFS_ENAMETOOLONG;
		output[position++] = '/';
	}
	output[position] = '\0';
	return 0;
}

static bool
strip_final_slash(char *path)
{
	size_t length = strlen(path);
	if (length <= 1 || path[length - 1] != '/')
		return false;
	path[length - 1] = '\0';
	return true;
}

int
vfs_resolve_path(const char *base_path, const char *path, char *output,
	size_t capacity)
{
	return vfs_resolve_path_as(base_path, path, output, capacity, 0, 0);
}

int
vfs_resolve_path_as(const char *base_path, const char *path, char *output,
	size_t capacity, uint32_t uid, uint32_t gid)
{
	char combined[VFS_PATH_MAX];
	char canonical[VFS_PATH_MAX];
	size_t base_length;
	size_t path_length;
	int error;

	if (path == NULL || output == NULL || capacity == 0)
		return -VFS_EINVAL;
	if (path[0] == '\0')
		return -VFS_ENOENT;
	if (path[0] == '/')
		error = canonicalize_path(path, canonical, uid, gid);
	else {
		if (base_path == NULL || base_path[0] != '/')
			return -VFS_EINVAL;
		base_length = strlen(base_path);
		path_length = strlen(path);
		if (base_length + (base_length > 1U ? 1U : 0U) + path_length >=
		    sizeof(combined))
			return -VFS_ENAMETOOLONG;
		memcpy(combined, base_path, base_length);
		if (base_length > 1U)
			combined[base_length++] = '/';
		memcpy(combined + base_length, path, path_length + 1U);
		error = canonicalize_path(combined, canonical, uid, gid);
	}
	if (error != 0)
		return error;
	base_length = strlen(canonical) + 1U;
	if (base_length > capacity)
		return -VFS_ENAMETOOLONG;
	memcpy(output, canonical, base_length);
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
	error = canonicalize_path(path, canonical, 0, 0);
	if (error != 0)
		return error;
	(void)strip_final_slash(canonical);
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
vfs_lookup_as(const char *path, uint32_t uid, uint32_t gid,
	struct vnode **result)
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
	error = canonicalize_path(path, canonical, uid, gid);
	if (error != 0)
		return error;
	mount = find_mount(canonical);
	if (mount == NULL)
		return -VFS_ENOENT;
	/* The mount shortcut must not bypass search rights in its ancestors. */
	if (mount->path_length > 1) {
		char parent_path[VFS_PATH_MAX];
		char *slash;
		struct vnode *parent = NULL;

		memcpy(parent_path, mount->path, mount->path_length + 1);
		slash = strrchr(parent_path, '/');
		if (slash == parent_path)
			slash[1] = '\0';
		else
			*slash = '\0';
		error = vfs_lookup_as(parent_path, uid, gid, &parent);
		if (error != 0)
			return error;
		if (parent->type != VNODE_DIRECTORY)
			error = -VFS_ENOTDIR;
		else if (!vfs_access(parent, uid, gid, VFS_ACCESS_EXEC))
			error = -VFS_EACCES;
		vnode_put(parent);
		if (error != 0)
			return error;
	}
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
		if (!vfs_access(current, uid, gid, VFS_ACCESS_EXEC)) {
			vnode_put(current);
			return -VFS_EACCES;
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
	if (canonical[strlen(canonical) - 1] == '/' &&
	    current->type != VNODE_DIRECTORY) {
		vnode_put(current);
		return -VFS_ENOTDIR;
	}
	*result = current;
	return 0;
}

int
vfs_lookup(const char *path, struct vnode **result)
{
	return vfs_lookup_as(path, 0, 0, result);
}

int
vfs_lookup_at_as(const char *base_path, const char *path,
	uint32_t uid, uint32_t gid, struct vnode **result)
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
		return vfs_lookup_as(path, uid, gid, result);
	if (path[0] == '\0')
		return -VFS_ENOENT;
	if (base_path == NULL)
		return -VFS_EINVAL;
	error = canonicalize_path(base_path, base, uid, gid);
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
	error = canonicalize_path(combined, canonical, uid, gid);
	if (error != 0)
		return error;
	return vfs_lookup_as(canonical, uid, gid, result);
}

int
vfs_lookup_at(const char *base_path, const char *path, struct vnode **result)
{
	return vfs_lookup_at_as(base_path, path, 0, 0, result);
}

int
vfs_create_path_as(const char *path, enum vnode_type type, uint32_t mode,
	uint32_t uid, uint32_t gid, struct vnode **result)
{
	char canonical[VFS_PATH_MAX];
	char parent_path[VFS_PATH_MAX];
	struct vnode *parent;
	bool parent_is_directory;
	const char *name;
	char *slash;
	uint32_t caller_gid = gid;
	int error;

	if (!initialized || result == NULL ||
	    (type != VNODE_REGULAR && type != VNODE_DIRECTORY &&
	    type != VNODE_SOCKET))
		return -VFS_EINVAL;
	*result = NULL;
	error = canonicalize_path(path, canonical, uid, gid);
	if (error != 0)
		return error;
	if (strip_final_slash(canonical) && type != VNODE_DIRECTORY)
		return -VFS_ENOTDIR;
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
	error = vfs_lookup_as(parent_path, uid, gid, &parent);
	if (error != 0)
		return error;
	if (parent->type != VNODE_DIRECTORY || parent->operations->create == NULL) {
		parent_is_directory = parent->type == VNODE_DIRECTORY;
		vnode_put(parent);
		return parent_is_directory ? -VFS_EROFS : -VFS_ENOTDIR;
	}
	if (!vfs_access(parent, uid, gid,
	    VFS_ACCESS_WRITE | VFS_ACCESS_EXEC)) {
		vnode_put(parent);
		return -VFS_EACCES;
	}
	if ((parent->mode & 02000U) != 0) {
		gid = parent->gid;
		if (type == VNODE_DIRECTORY)
			mode |= 02000U;
	}
	/* Inheriting a directory's group does not grant the caller the right
	 * to create a setgid executable for that group. */
	if (uid != 0 && type == VNODE_REGULAR && gid != caller_gid)
		mode &= ~02000U;
	error = parent->operations->create(parent, name, strlen(name), type, mode,
	    uid, gid, result);
	vnode_put(parent);
	return error;
}

int
vfs_create_path(const char *path, enum vnode_type type, uint32_t mode,
	struct vnode **result)
{
	return vfs_create_path_as(path, type, mode, 0, 0, result);
}

int
vfs_remove_path_as(const char *path, bool directory, uint32_t uid,
	uint32_t gid)
{
	char canonical[VFS_PATH_MAX], parent_path[VFS_PATH_MAX];
	struct vnode *parent = NULL;
	struct vfs_mount *mount;
	char *slash;
	const char *name;
	int error;
	if (!initialized || path == NULL)
		return -VFS_EINVAL;
	error = canonicalize_path(path, canonical, uid, gid);
	if (error != 0) return error;
	if (strcmp(canonical, "/") == 0) return -VFS_EBUSY;
	if (canonical[strlen(canonical) - 1] == '/') {
		struct vnode *target = NULL;
		error = vfs_lookup_as(canonical, uid, gid, &target);
		if (error != 0)
			return error;
		vnode_put(target);
		(void)strip_final_slash(canonical);
	}
	for (mount = mounts; mount != NULL; mount = mount->next)
		if (strcmp(mount->path, canonical) == 0) return -VFS_EBUSY;
	slash = strrchr(canonical, '/');
	name = slash + 1;
	if (slash == canonical) {
		parent_path[0] = '/'; parent_path[1] = '\0';
	} else {
		size_t n = (size_t)(slash - canonical);
		memcpy(parent_path, canonical, n); parent_path[n] = '\0';
	}
	error = vfs_lookup_as(parent_path, uid, gid, &parent);
	if (error != 0) return error;
	if (parent->type != VNODE_DIRECTORY || parent->operations->remove == NULL)
		error = parent->type == VNODE_DIRECTORY ? -VFS_EROFS : -VFS_ENOTDIR;
	else if (!vfs_access(parent, uid, gid,
	    VFS_ACCESS_WRITE | VFS_ACCESS_EXEC))
		error = -VFS_EACCES;
	else {
		/* A writable sticky directory still protects other users' entries. */
		if ((parent->mode & 01000U) != 0 && uid != 0 &&
		    uid != parent->uid) {
			struct vnode *target = NULL;
			if (parent->operations->lookup == NULL)
				error = -VFS_EIO;
			else
				error = parent->operations->lookup(parent, name,
				    strlen(name), &target);
			if (error == 0)
				error = target == NULL ? -VFS_EIO :
				    uid == target->uid ? 0 : -VFS_EPERM;
			vnode_put(target);
		}
		if (error == 0)
			error = parent->operations->remove(parent, name,
			    strlen(name), directory);
	}
	vnode_put(parent);
	return error;
}

int
vfs_stat_path_as(const char *path, uint32_t uid, uint32_t gid,
	struct vfs_stat *status)
{
	struct vnode *node;
	int error;

	if (status == NULL)
		return -VFS_EINVAL;
	error = vfs_lookup_as(path, uid, gid, &node);
	if (error != 0)
		return error;
	*status = (struct vfs_stat){
		.size = node->size,
		.mode = node->mode,
		.type = (uint32_t)node->type,
		.uid = node->uid,
		.gid = node->gid
	};
	vnode_put(node);
	return 0;
}

int
vfs_stat_path(const char *path, struct vfs_stat *status)
{
	return vfs_stat_path_as(path, 0, 0, status);
}

bool
vfs_access(const struct vnode *node, uint32_t uid, uint32_t gid,
	unsigned int requested)
{
	unsigned int bits;
	if (node == NULL || (requested & ~(VFS_ACCESS_EXEC | VFS_ACCESS_WRITE |
	    VFS_ACCESS_READ)) != 0)
		return false;
	if (uid == 0) {
		if ((requested & VFS_ACCESS_EXEC) != 0 &&
		    node->type == VNODE_REGULAR && (node->mode & 0111U) == 0)
			return false;
		return true;
	}
	if (uid == node->uid)
		bits = (node->mode >> 6) & 7U;
	else if (gid == node->gid)
		bits = (node->mode >> 3) & 7U;
	else
		bits = node->mode & 7U;
	return ((requested & VFS_ACCESS_READ) == 0 || (bits & 4U) != 0) &&
	    ((requested & VFS_ACCESS_WRITE) == 0 || (bits & 2U) != 0) &&
	    ((requested & VFS_ACCESS_EXEC) == 0 || (bits & 1U) != 0);
}

int
vfs_chmod_node_as(struct vnode *node, uint32_t mode, uint32_t uid,
	uint32_t gid)
{
	int error;
	if (node == NULL)
		return -VFS_EINVAL;
	if (uid != 0 && uid != node->uid) {
		return -VFS_EACCES;
	}
	if (uid != 0 && (mode & 02000U) != 0 && gid != node->gid)
		return -VFS_EACCES;
	if (node->operations->chmod == NULL)
		return -VFS_EROFS;
	error = node->operations->chmod(node, mode & 07777U);
	return error;
}

int
vfs_chmod_as(const char *path, uint32_t mode, uint32_t uid, uint32_t gid)
{
	struct vnode *node;
	int error = vfs_lookup_as(path, uid, gid, &node);
	if (error != 0)
		return error;
	error = vfs_chmod_node_as(node, mode, uid, gid);
	vnode_put(node);
	return error;
}

int
vfs_chown_as(const char *path, uint32_t owner, uint32_t group,
	uint32_t euid, uint32_t egid)
{
	struct vnode *node;
	int error;
	(void)egid;
	if (euid != 0)
		return -VFS_EPERM;
	error = vfs_lookup_as(path, euid, 0, &node);
	if (error != 0)
		return error;
	if (node->operations->chown == NULL)
		error = -VFS_EROFS;
	else
		error = node->operations->chown(node,
		    owner == UINT32_MAX ? node->uid : owner,
		    group == UINT32_MAX ? node->gid : group);
	vnode_put(node);
	return error;
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

	if (file->node->type == VNODE_DIRECTORY && whence == VFS_SEEK_SET &&
	    offset == 0) {
		*result = 0;
		return 0;
	}
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
		.type = (uint32_t)node->type,
		.uid = node->uid,
		.gid = node->gid
	};
	return 0;
}

static int
file_chmod(void *context, uint32_t mode)
{
	struct vfs_file_context *file = context;
	return file->node->operations->chmod == NULL ? -VFS_EROFS :
	    file->node->operations->chmod(file->node, mode & 07777U);
}

static int
file_chown(void *context, uint32_t uid, uint32_t gid)
{
	struct vfs_file_context *file = context;
	struct vnode *node = file->node;
	return node->operations->chown == NULL ? -VFS_EROFS :
	    node->operations->chown(node, uid == UINT32_MAX ? node->uid : uid,
	    gid == UINT32_MAX ? node->gid : gid);
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
	.chmod = file_chmod,
	.chown = file_chown,
	.get_path = file_get_path,
	.close = file_close
};

int
vfs_open_as(const char *path, uint32_t flags, uint32_t mode,
	uint32_t uid, uint32_t gid, struct fd_object **result)
{
	const uint32_t allowed = VFS_O_ACCMODE | VFS_O_CREAT | VFS_O_EXCL | VFS_O_TRUNC |
	    VFS_O_DIRECTORY | VFS_O_NONBLOCK;
	struct vfs_file_context *context;
	struct vnode *node = NULL;
	struct fd_object *file;
	char canonical[VFS_PATH_MAX];
	bool created = false;
	int error;

	if (result == NULL || (flags & ~allowed) != 0 ||
	    (flags & VFS_O_ACCMODE) == VFS_O_ACCMODE ||
	    ((flags & (VFS_O_CREAT | VFS_O_DIRECTORY)) ==
	    (VFS_O_CREAT | VFS_O_DIRECTORY)) ||
	    ((flags & VFS_O_TRUNC) != 0 &&
	    (flags & VFS_O_ACCMODE) == VFS_O_RDONLY))
		return -VFS_EINVAL;
	*result = NULL;
	error = canonicalize_path(path, canonical, uid, gid);
	if (error != 0)
		return error;
	error = vfs_lookup_as(canonical, uid, gid, &node);
	if (error == 0 && (flags & (VFS_O_CREAT | VFS_O_EXCL)) ==
	    (VFS_O_CREAT | VFS_O_EXCL)) {
		vnode_put(node);
		return -VFS_EEXIST;
	}
	if (error == -VFS_ENOENT && (flags & VFS_O_CREAT) != 0) {
		error = vfs_create_path_as(path, VNODE_REGULAR, mode, uid, gid,
		    &node);
		created = error == 0;
	}
	if (error != 0)
		return error;
	if (node->type == VNODE_SOCKET) {
		vnode_put(node);
		return -VFS_ENXIO;
	}
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
	{
		unsigned int requested = 0;
		if ((flags & VFS_O_ACCMODE) == VFS_O_RDONLY ||
		    (flags & VFS_O_ACCMODE) == VFS_O_RDWR)
			requested |= VFS_ACCESS_READ;
		if ((flags & VFS_O_ACCMODE) == VFS_O_WRONLY ||
		    (flags & VFS_O_ACCMODE) == VFS_O_RDWR)
			requested |= VFS_ACCESS_WRITE;
		/* The caller is authorized by the parent-directory check performed
		 * during creation. Do not reject the descriptor it just created
		 * because the requested mode is more restrictive than its access. */
		if (!created && !vfs_access(node, uid, gid, requested)) {
			vnode_put(node);
			return -VFS_EACCES;
		}
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

int
vfs_open(const char *path, uint32_t flags, uint32_t mode,
	struct fd_object **result)
{
	return vfs_open_as(path, flags, mode, 0, 0, result);
}
