#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/heap.h"
#include "kernel/vfs.h"

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *allocation) { free(allocation); return true; }

static void
create(const char *path, enum vnode_type type, uint32_t mode, uint32_t uid)
{
	struct vnode *node = NULL;
	assert(vfs_create_path(path, type, mode, &node) == 0);
	assert(node->operations->chown(node, uid, uid) == 0);
	assert(node->operations->chmod(node, mode) == 0);
	vnode_put(node);
}

static void
sticky_removal(void)
{
	struct vnode *node = NULL;
	create("/tmp", VNODE_DIRECTORY, 01777, 100);
	create("/tmp/file", VNODE_REGULAR, 0666, 200);
	create("/tmp/dir", VNODE_DIRECTORY, 0777, 200);
	assert(vfs_remove_path_as("/tmp/file", false, 300, 300) == -1);
	assert(vfs_remove_path_as("/tmp/dir", true, 300, 300) == -1);
	assert(vfs_lookup("/tmp/file", &node) == 0);
	vnode_put(node);
	assert(vfs_remove_path_as("/tmp/file", false, 200, 200) == 0);
	assert(vfs_remove_path_as("/tmp/dir", true, 100, 100) == 0);
	create("/tmp/root-removal", VNODE_REGULAR, 0000, 200);
	assert(vfs_remove_path_as("/tmp/root-removal", false, 0, 0) == 0);
	assert(vfs_remove_path_as("/tmp/missing", false, 300, 300) == -2);
	create("/shared", VNODE_DIRECTORY, 0777, 100);
	create("/shared/file", VNODE_REGULAR, 0000, 200);
	assert(vfs_remove_path_as("/shared/file", false, 300, 300) == 0);
	create("/locked", VNODE_DIRECTORY, 01755, 100);
	create("/locked/file", VNODE_REGULAR, 0666, 200);
	assert(vfs_remove_path_as("/locked/file", false, 200, 200) == -13);
}

int main(void)
{
	struct vnode *root;
	assert(vfs_init());
	root = ramfs_create_root();
	assert(root != NULL);
	assert(vfs_mount("/", root) == 0);
	vnode_put(root);
	sticky_removal();
	puts("VFS permission tests passed");
	return 0;
}
