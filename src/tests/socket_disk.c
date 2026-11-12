#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/heap.h"
#include "kernel/silliconfs.h"

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *allocation) { free(allocation); return true; }
static int read_block(void *context, uint32_t number, void *block)
{
	FILE *file = context;
	return fseek(file, (long)number * SILLICONFS_BLOCK_SIZE, SEEK_SET) == 0 &&
	    fread(block, 1, SILLICONFS_BLOCK_SIZE, file) == SILLICONFS_BLOCK_SIZE ?
	    0 : -5;
}
static int write_block(void *context, uint32_t number, const void *block)
{
	(void)context; (void)number; (void)block;
	return -5;
}
int main(int argc, char **argv)
{
	struct vnode *root, *directory;
	struct vfs_stat status;
	struct vfs_dirent entry;
	bool found = false;
	assert(argc == 2);
	FILE *file = fopen(argv[1], "rb");
	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	long size = ftell(file);
	assert(size > 0 && size % SILLICONFS_BLOCK_SIZE == 0);
	struct silliconfs_device device = {
		.context = file, .block_count = (uint32_t)(size / SILLICONFS_BLOCK_SIZE),
		.read_block = read_block, .write_block = write_block
	};
	/* Mount validation must accept the socket left by the QEMU guest. */
	assert(vfs_init());
	assert(silliconfs_mount(&device, &root) == 0);
	assert(vfs_mount("/", root) == 0);
	vnode_put(root);
	assert(vfs_stat_path("/tmp/protected-socket", &status) == 0);
	assert(status.type == VNODE_SOCKET && status.mode == 0600 && status.uid == 0);
	assert(vfs_lookup("/tmp", &directory) == 0);
	for (uint64_t i = 0; directory->operations->readdir(directory, i, &entry) == 1;
	    i++) {
		if (strcmp(entry.name, "protected-socket") == 0) {
			assert(entry.type == VNODE_SOCKET);
			found = true;
		}
	}
	assert(found);
	vnode_put(directory);
	assert(fclose(file) == 0);
	puts("Persisted socket inode tests passed");
	return 0;
}
