#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/vfs.h"

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *allocation) { free(allocation); return true; }
void klog(const char *message) { fputs(message, stderr); abort(); }
_Noreturn void kernel_halt(void) { abort(); }

int main(int argc, char **argv)
{
	struct vnode *root, *node, *expected;
	struct fd_object *directory;
	assert(argc == 2);
	FILE *input = fopen(argv[1], "rb");
	assert(input != NULL && fseek(input, 0, SEEK_END) == 0);
	long size = ftell(input);
	assert(size > 0 && fseek(input, 0, SEEK_SET) == 0);
	void *archive = malloc((size_t)size);
	assert(archive != NULL && fread(archive, 1, (size_t)size, input) == (size_t)size);
	assert(fclose(input) == 0);
	assert(vfs_init());
	root = ramfs_create_root();
	assert(root != NULL && vfs_mount("/", root) == 0);
	vnode_put(root);
	assert(vfs_create_path("/init", VNODE_DIRECTORY, 0755, &node) == 0);
	vnode_put(node);
	assert(initramfs_mount(archive, (size_t)size, &root) == 0);
	assert(vfs_mount("/init", root) == 0);
	vnode_put(root);
	assert(vfs_lookup("/init/public/file", &expected) == 0);
	assert(vfs_open("/init/private/inner", VFS_O_DIRECTORY, 0, &directory) == 0);
	assert(fd_object_lookup_at_as(directory, "file", 100, 100, &node) == 0);
	vnode_put(node);
	assert(fd_object_lookup_at_as(directory, "../.", 100, 100, &node) == -13);
	assert(fd_object_lookup_at_as(directory, "../../public/file", 100, 100, &node) == -13);
	assert(fd_object_lookup_at_as(directory, "../../public/file", 0, 0, &node) == 0);
	assert(node == expected);
	vnode_put(node);
	assert(fd_object_lookup_at_as(directory, "../../../init/public/file", 0, 0, &node) == 0);
	assert(node == expected);
	vnode_put(node);
	assert(fd_object_lookup_at_as(directory, "file/..", 0, 0, &node) == -20);
	vnode_put(expected);
	fd_object_put(directory);
	puts("Initramfs relative directory tests passed");
	return 0;
}
