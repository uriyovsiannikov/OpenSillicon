#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/fd.h"
#include "kernel/vfs.h"

static size_t live_allocations;
void *kheap_alloc(size_t size)
{
	void *allocation = malloc(size);
	if (allocation != NULL) live_allocations++;
	return allocation;
}
bool kheap_free(void *allocation)
{
	if (allocation != NULL) {
		assert(live_allocations > 0);
		live_allocations--;
	}
	free(allocation);
	return true;
}
void klog(const char *message) { fputs(message, stderr); abort(); }
_Noreturn void kernel_halt(void) { abort(); }

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

static void
directory_anchors(void)
{
	size_t before = live_allocations;
	struct vnode *node = NULL;
	struct fd_object *anchor = NULL;
	uint64_t parent_inode, child_inode;
	create("/anchor-parent", VNODE_DIRECTORY, 0700, 0);
	create("/anchor-parent/child", VNODE_DIRECTORY, 0777, 0);
	create("/anchor-parent/child/file", VNODE_REGULAR, 0644, 0);
	assert(vfs_lookup("/anchor-parent", &node) == 0);
	parent_inode = node->inode;
	vnode_put(node);
	assert(vfs_open("/anchor-parent/child", VFS_O_RDONLY, 0, &anchor) == 0);
	/* The descriptor starts at child; its ancestors need not be searchable. */
	assert(fd_object_lookup_at_as(anchor, "file", 100, 100, &node) == 0);
	vnode_put(node);
	assert(fd_object_lookup_at_as(anchor, "file/", 100, 100, &node) == -20);
	assert(fd_object_lookup_at_as(anchor, ".", 100, 100, &node) == 0);
	child_inode = node->inode;
	vnode_put(node);
	assert(vfs_chmod_as("/anchor-parent/child", 0700, 0, 0) == 0);
	assert(fd_object_lookup_at_as(anchor, ".", 100, 100, &node) == -13);
	assert(vfs_chmod_as("/anchor-parent/child", 0777, 0, 0) == 0);
	assert(vfs_remove_path_as("/anchor-parent/child/file", false, 0, 0) == 0);
	assert(vfs_remove_path_as("/anchor-parent/child", true, 0, 0) == 0);
	assert(vfs_remove_path_as("/anchor-parent", true, 0, 0) == 0);
	create("/anchor-parent", VNODE_DIRECTORY, 0755, 0);
	create("/anchor-parent/child", VNODE_DIRECTORY, 0755, 0);
	assert(fd_object_lookup_at_as(anchor, ".", 100, 100, &node) == 0);
	assert(node->inode == child_inode);
	vnode_put(node);
	assert(fd_object_lookup_at_as(anchor, "..", 100, 100, &node) == 0);
	assert(node->inode == parent_inode);
	vnode_put(node);
	assert(fd_object_lookup_at_as(anchor, "../.", 100, 100, &node) == -13);
	assert(fd_object_lookup_at_as(anchor, "file", 100, 100, &node) == -2);
	fd_object_put(anchor);
	assert(vfs_remove_path_as("/anchor-parent/child", true, 0, 0) == 0);
	assert(vfs_remove_path_as("/anchor-parent", true, 0, 0) == 0);
	assert(live_allocations == before);
}

static void
setid_writes(void)
{
	struct vnode *node = NULL;
	struct fd_object *file = NULL;
	size_t amount = 100;
	create("/setid-write", VNODE_REGULAR, 06777, 0);
	assert(vfs_lookup("/setid-write", &node) == 0);
	assert(vfs_open_as("/setid-write", VFS_O_WRONLY, 0, 0, 0, &file) == 0);
	assert(fd_object_write_as(file, "R", 1, &amount, 0) == 0 && amount == 1);
	assert(node->mode == 06777);
	assert(fd_object_write_as(file, "", 0, &amount, 100) == 0 && amount == 0);
	assert(node->mode == 06777);
	assert(fd_object_write_as(file, "U", 1, &amount, 100) == 0 && amount == 1);
	assert(node->mode == 0777);
	fd_object_put(file);
	assert(vfs_chmod_as("/setid-write", 06777, 0, 0) == 0);
	assert(vfs_open_as("/setid-write", VFS_O_RDONLY, 0, 100, 100, &file) == 0);
	assert(fd_object_write_as(file, "X", 1, &amount, 100) == -9);
	assert(node->mode == 06777);
	fd_object_put(file);
	assert(vfs_open_as("/setid-write", VFS_O_WRONLY | VFS_O_TRUNC, 0,
	    100, 100, &file) == 0);
	assert(node->mode == 0777 && node->size == 0);
	fd_object_put(file);
	vnode_put(node);
}

static void
dot_removal(void)
{
	struct vnode *node = NULL;
	create("/dotdir", VNODE_DIRECTORY, 0755, 0);
	assert(vfs_remove_path_as("/dotdir/.", true, 0, 0) == -22);
	assert(vfs_remove_path_as("/dotdir/.//", true, 0, 0) == -22);
	assert(vfs_remove_path_as("/dotdir/..", true, 0, 0) == -22);
	assert(vfs_remove_path_as("/dotdir/.", false, 0, 0) == -21);
	assert(vfs_lookup("/dotdir", &node) == 0);
	vnode_put(node);
	assert(vfs_remove_path_as("/dotdir", true, 0, 0) == 0);
	create("/...", VNODE_DIRECTORY, 0755, 0);
	assert(vfs_remove_path_as("/...", true, 0, 0) == 0);
}

static void
trailing_slashes(void)
{
	struct vnode *node = NULL;
	char resolved[VFS_PATH_MAX];
	create("/slash-file", VNODE_REGULAR, 0644, 100);
	assert(vfs_lookup_as("/slash-file/", 100, 100, &node) == -20);
	assert(node == NULL);
	assert(vfs_chmod_as("/slash-file//", 0777, 100, 100) == -20);
	assert(vfs_remove_path_as("/slash-file/", false, 0, 0) == -20);
	assert(vfs_lookup("/slash-file", &node) == 0);
	assert(node->mode == 0644);
	vnode_put(node);
	assert(vfs_create_path("/slash-new/", VNODE_REGULAR, 0600, &node) == -20);
	assert(vfs_lookup("/slash-new", &node) == -2);
	assert(vfs_create_path("/slash-dir/", VNODE_DIRECTORY, 0755, &node) == 0);
	vnode_put(node);
	assert(vfs_lookup("/slash-dir//", &node) == 0);
	vnode_put(node);
	assert(vfs_remove_path_as("/slash-dir/", true, 0, 0) == 0);
	assert(vfs_resolve_path("/", "slash-file/", resolved, sizeof(resolved)) == 0);
	assert(strcmp(resolved, "/slash-file/") == 0);
	assert(vfs_lookup("/", &node) == 0);
	vnode_put(node);
}

static void
setgid_creation(void)
{
	struct vnode *parent = NULL, *node = NULL;
	assert(vfs_create_path("/group", VNODE_DIRECTORY, 02777, &parent) == 0);
	assert(parent->operations->chown(parent, 100, 500) == 0);
	assert(parent->operations->chmod(parent, 02777) == 0);
	vnode_put(parent);
	assert(vfs_create_path_as("/group/nonmember", VNODE_REGULAR, 02755,
	    200, 200, &node) == 0);
	assert(node->gid == 500);
	assert(node->mode == 0755);
	vnode_put(node);
	assert(vfs_create_path_as("/group/member", VNODE_REGULAR, 02755,
	    200, 500, &node) == 0);
	assert(node->gid == 500 && node->mode == 02755);
	vnode_put(node);
	assert(vfs_create_path_as("/group/root", VNODE_REGULAR, 02755,
	    0, 0, &node) == 0);
	assert(node->gid == 500 && node->mode == 02755);
	vnode_put(node);
	assert(vfs_create_path_as("/group/subdir", VNODE_DIRECTORY, 0755,
	    200, 200, &node) == 0);
	assert(node->gid == 500 && node->mode == 02755);
	vnode_put(node);
}

static void
dot_components(void)
{
	struct vnode *node = NULL;
	char resolved[VFS_PATH_MAX];
	create("/closed", VNODE_DIRECTORY, 0700, 100);
	create("/public", VNODE_REGULAR, 0644, 0);
	assert(vfs_lookup_as("/closed/../public", 200, 200, &node) == -13);
	assert(node == NULL);
	assert(vfs_lookup_as("/closed/.", 200, 200, &node) == -13);
	assert(vfs_lookup_as("/absent/../public", 200, 200, &node) == -2);
	assert(vfs_lookup_as("/public/../public", 200, 200, &node) == -20);
	assert(vfs_lookup_as("/public/.", 200, 200, &node) == -20);
	assert(vfs_lookup_at_as("/closed", "../public", 200, 200, &node) == -13);
	assert(vfs_lookup_as("/closed/../public", 100, 100, &node) == 0);
	vnode_put(node);
	assert(vfs_lookup_as("/../../public", 200, 200, &node) == 0);
	vnode_put(node);
	assert(vfs_resolve_path_as("/closed", "../public", resolved,
	    sizeof(resolved), 200, 200) == -13);
	assert(vfs_resolve_path_as("/closed", "../public", resolved,
	    sizeof(resolved), 100, 100) == 0);
	assert(strcmp(resolved, "/public") == 0);
	assert(vfs_remove_path_as("/closed/../public", false, 200, 200) == -13);
	assert(vfs_create_path_as("/closed/../new", VNODE_REGULAR, 0600,
	    200, 200, &node) == -13);
}

static void
mount_ancestors(void)
{
	struct vnode *mounted = ramfs_create_root();
	struct vnode *node = NULL;
	assert(mounted != NULL);
	create("/private", VNODE_DIRECTORY, 0700, 100);
	create("/private/mnt", VNODE_DIRECTORY, 0755, 0);
	assert(vfs_mount("/private/mnt", mounted) == 0);
	vnode_put(mounted);
	create("/private/mnt/file", VNODE_REGULAR, 0644, 0);
	assert(vfs_lookup_as("/private/mnt/file", 200, 200, &node) == -13);
	assert(node == NULL);
	assert(vfs_lookup_as("/private/mnt", 200, 200, &node) == -13);
	assert(vfs_lookup_as("/private/mnt/file", 100, 100, &node) == 0);
	vnode_put(node);
	assert(vfs_lookup_as("/private/mnt/file", 0, 0, &node) == 0);
	vnode_put(node);
	assert(vfs_chmod_as("/private", 0755, 100, 100) == 0);
	assert(vfs_lookup_as("/private/mnt/file", 200, 200, &node) == 0);
	vnode_put(node);
	struct fd_object *anchor;
	assert(vfs_open("/private", VFS_O_RDONLY, 0, &anchor) == 0);
	assert(fd_object_lookup_at_as(anchor, "mnt/../mnt/file", 200, 200, &node) == 0);
	vnode_put(node);
	fd_object_put(anchor);
	assert(vfs_open("/private/mnt", VFS_O_RDONLY, 0, &anchor) == 0);
	assert(fd_object_lookup_at_as(anchor, "../../private/mnt/file", 200, 200, &node) == 0);
	vnode_put(node);
	fd_object_put(anchor);
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
	directory_anchors();
	setid_writes();
	dot_removal();
	trailing_slashes();
	setgid_creation();
	dot_components();
	mount_ancestors();
	puts("VFS permission tests passed");
	return 0;
}
