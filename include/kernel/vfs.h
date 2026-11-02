#ifndef KERNEL_VFS_H
#define KERNEL_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VFS_PATH_MAX 256U
#define VFS_NAME_MAX 255U

#define VFS_O_RDONLY 0U
#define VFS_O_WRONLY 1U
#define VFS_O_RDWR 2U
#define VFS_O_ACCMODE 3U
#define VFS_O_CREAT 0x40U
#define VFS_O_TRUNC 0x200U
#define VFS_O_DIRECTORY 0x10000U

struct fd_object;
struct vnode;

enum vnode_type {
	VNODE_REGULAR = 1,
	VNODE_DIRECTORY = 2
};

struct vfs_stat {
	uint64_t size;
	uint32_t mode;
	uint32_t type;
};

struct vfs_dirent {
	uint64_t inode;
	uint64_t size;
	uint32_t mode;
	uint32_t type;
	char name[VFS_NAME_MAX + 1];
};

struct vnode_operations {
	int (*lookup)(struct vnode *directory, const char *name, size_t name_length,
	    struct vnode **result);
	int (*create)(struct vnode *directory, const char *name,
	    size_t name_length, enum vnode_type type, uint32_t mode,
	    struct vnode **result);
	int (*read)(struct vnode *node, uint64_t offset, void *buffer,
	    size_t length, size_t *read_length);
	int (*write)(struct vnode *node, uint64_t offset, const void *buffer,
	    size_t length, size_t *write_length);
	int (*truncate)(struct vnode *node, uint64_t size);
	int (*readdir)(struct vnode *directory, uint64_t index,
	    struct vfs_dirent *entry);
	void (*destroy)(struct vnode *node);
};

struct vnode {
	const struct vnode_operations *operations;
	enum vnode_type type;
	uint32_t mode;
	uint64_t size;
	uint64_t inode;
	uint32_t references;
	void *private_data;
};

bool vfs_init(void);
bool vnode_get(struct vnode *node);
void vnode_put(struct vnode *node);
uint64_t vfs_allocate_inode(void);
int vfs_mount(const char *path, struct vnode *root);
int vfs_replace_root(struct vnode *root);
int vfs_lookup(const char *path, struct vnode **result);
int vfs_lookup_at(const char *base_path, const char *path,
	struct vnode **result);
int vfs_create_path(const char *path, enum vnode_type type, uint32_t mode,
	struct vnode **result);
int vfs_stat_path(const char *path, struct vfs_stat *status);
int vfs_open(const char *path, uint32_t flags, uint32_t mode,
	struct fd_object **result);
struct vnode *ramfs_create_root(void);
int initramfs_mount(const void *archive, size_t archive_size,
	struct vnode **root);

#endif
