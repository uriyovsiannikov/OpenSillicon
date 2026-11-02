#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/vfs.h"

void *
kheap_alloc(size_t size)
{
	return malloc(size);
}

bool
kheap_free(void *allocation)
{
	free(allocation);
	return true;
}

bool
serial_write(const char *buffer, size_t length)
{
	return fwrite(buffer, 1, length, stdout) == length;
}

void
klog(const char *message)
{
	fputs(message, stderr);
}

void
klog_u64(uint64_t value)
{
	(void)value;
}

_Noreturn void
kernel_halt(void)
{
	abort();
}

static uint8_t *
read_archive(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	long file_size;
	uint8_t *archive;

	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	file_size = ftell(file);
	assert(file_size > 0);
	assert(fseek(file, 0, SEEK_SET) == 0);
	archive = malloc((size_t)file_size);
	assert(archive != NULL);
	assert(fread(archive, 1, (size_t)file_size, file) == (size_t)file_size);
	assert(fclose(file) == 0);
	*size = (size_t)file_size;
	return archive;
}

static uint32_t
read_hex32(const uint8_t *bytes)
{
	char text[9];
	char *end;
	unsigned long value;

	memcpy(text, bytes, 8);
	text[8] = '\0';
	value = strtoul(text, &end, 16);
	assert(*end == '\0' && value <= UINT32_MAX);
	return (uint32_t)value;
}

static void
write_hex32(uint8_t *bytes, uint32_t value)
{
	char text[9];

	(void)snprintf(text, sizeof(text), "%08X", value);
	memcpy(bytes, text, 8);
}

static size_t
find_record(const uint8_t *archive, size_t archive_size, const char *name)
{
	size_t offset = 0;

	while (archive_size - offset >= 110) {
		uint32_t file_size = read_hex32(archive + offset + 54);
		uint32_t name_size = read_hex32(archive + offset + 94);
		size_t name_offset = offset + 110;
		size_t data_offset = (name_offset + name_size + 3) & ~(size_t)3;
		size_t next_offset = (data_offset + file_size + 3) & ~(size_t)3;

		assert(name_size <= archive_size - name_offset);
		assert(next_offset <= archive_size);
		if (strlen(name) + 1 == name_size &&
		    memcmp(archive + name_offset, name, name_size) == 0)
			return offset;
		offset = next_offset;
	}
	return SIZE_MAX;
}

static void
test_archive_validation(const uint8_t *archive, size_t archive_size)
{
	struct vnode *root = NULL;
	uint8_t *copy = malloc(archive_size);
	size_t i;

	assert(copy != NULL);
	assert(initramfs_mount(archive, 100, &root) == -22);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	copy[0] = 'X';
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	copy[6] = 'G';
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	memcpy(copy, "070702", 6);
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	copy[94] = 'F';
	copy[95] = 'F';
	copy[96] = 'F';
	copy[97] = 'F';
	copy[98] = 'F';
	copy[99] = 'F';
	copy[100] = 'F';
	copy[101] = 'F';
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	memset(copy + 54, 'F', 8);
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	copy[110] = '.';
	copy[111] = '.';
	copy[112] = '/';
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	{
		size_t record = find_record(copy, archive_size, "etc/motd");

		assert(record != SIZE_MAX);
		write_hex32(copy + record + 14, 0120777U);
		assert(initramfs_mount(copy, archive_size, &root) == -5);
		assert(root == NULL);
		memcpy(copy, archive, archive_size);
		write_hex32(copy + record + 38, 2);
		assert(initramfs_mount(copy, archive_size, &root) == -5);
		assert(root == NULL);
	}
	memcpy(copy, archive, archive_size);
	i = find_record(copy, archive_size, "TRAILER!!!");
	assert(i != SIZE_MAX);
	copy[i] = 'X';
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	memcpy(copy, archive, archive_size);
	copy[archive_size - 1] = 1;
	assert(initramfs_mount(copy, archive_size, &root) == -5);
	assert(root == NULL);
	free(copy);
}

static void
test_vfs(const uint8_t *archive, size_t archive_size)
{
	static const char contents[] =
	    "OpenSillicon initramfs is mounted read-only.\n";
	static const char new_data[] = "ramfs data";
	struct fd_table parent;
	struct fd_table child;
	struct fd_object *file;
	struct vnode *root;
	struct vnode *initrd;
	struct vnode *created;
	struct vfs_stat status;
	struct vfs_dirent entry;
	char buffer[sizeof(contents)];
	size_t amount;
	int descriptor;

	assert(vfs_init());
	root = ramfs_create_root();
	assert(root != NULL);
	assert(vfs_mount("/", root) == 0);
	vnode_put(root);
	assert(vfs_create_path("/init", VNODE_DIRECTORY, 0755, &created) == 0);
	vnode_put(created);
	assert(vfs_create_path("/tmp", VNODE_DIRECTORY, 01777, &created) == 0);
	vnode_put(created);
	assert(initramfs_mount(archive, archive_size, &initrd) == 0);
	assert(vfs_mount("/init", initrd) == 0);
	vnode_put(initrd);

	assert(vfs_stat_path("/init/../init/etc/motd", &status) == 0);
	assert(status.type == VNODE_REGULAR && status.size == sizeof(contents) - 1);
	assert(vfs_stat_path("/init", &status) == 0 &&
	    status.type == VNODE_DIRECTORY);
	assert(vfs_lookup("relative/path", &created) == -22);
	assert(vfs_open("/init/etc/missing", VFS_O_RDONLY, 0, &file) == -2);
	assert(vfs_open("/init/etc/motd", VFS_O_WRONLY, 0, &file) == -30);
	assert(vfs_open("/tmp/invalid", VFS_O_CREAT | VFS_O_DIRECTORY,
	    0755, &file) == -22);
	assert(vfs_create_path("/init/etc/new", VNODE_REGULAR, 0644,
	    &created) == -30);
	assert(vfs_open("/init/etc", VFS_O_RDONLY | VFS_O_DIRECTORY, 0,
	    &file) == 0);
	fd_table_init_standard(&parent);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	assert(fd_readdir(&parent, (uint64_t)descriptor, &entry) == 1);
	assert(strcmp(entry.name, "motd") == 0 && entry.type == VNODE_REGULAR);
	assert(fd_readdir(&parent, (uint64_t)descriptor, &entry) == 0);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);

	assert(vfs_open("/init/etc/motd", VFS_O_RDONLY, 0, &file) == 0);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	assert(fd_table_clone(&child, &parent));
	assert(fd_read(&parent, (uint64_t)descriptor, buffer, 7, &amount) == 0);
	assert(amount == 7 && memcmp(buffer, contents, 7) == 0);
	assert(fd_read(&child, (uint64_t)descriptor, buffer, sizeof(buffer),
	    &amount) == 0);
	assert(amount == sizeof(contents) - 1 - 7);
	assert(memcmp(buffer, contents + 7, amount) == 0);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	assert(fd_read(&child, (uint64_t)descriptor, buffer, sizeof(buffer),
	    &amount) == 0 && amount == 0);
	fd_table_destroy(&parent);
	fd_table_destroy(&child);

	assert(vfs_create_path("/tmp/sequence-a", VNODE_REGULAR, 0644,
	    &created) == 0);
	vnode_put(created);
	assert(vfs_create_path("/tmp/sequence-b", VNODE_REGULAR, 0644,
	    &created) == 0);
	vnode_put(created);
	assert(vfs_lookup_at("/tmp", "sequence-a", &created) == 0);
	assert(created->type == VNODE_REGULAR && created->size == 0);
	vnode_put(created);
	assert(vfs_lookup_at("/tmp", "../tmp/sequence-b", &created) == 0);
	assert(created->type == VNODE_REGULAR);
	vnode_put(created);
	assert(vfs_lookup_at("/tmp", "/tmp/sequence-a", &created) == 0);
	vnode_put(created);
	assert(vfs_lookup_at("/tmp", "missing", &created) == -ENOENT);
	assert(vfs_open("/tmp", VFS_O_RDONLY | VFS_O_DIRECTORY, 0, &file) == 0);
	fd_table_init_standard(&parent);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3 && fd_table_clone(&child, &parent));
	assert(fd_readdir(&parent, (uint64_t)descriptor, &entry) == 1);
	assert(strcmp(entry.name, "sequence-a") == 0);
	assert(vfs_create_path("/tmp/sequence-c", VNODE_REGULAR, 0644,
	    &created) == 0);
	vnode_put(created);
	assert(fd_readdir(&child, (uint64_t)descriptor, &entry) == 1);
	assert(strcmp(entry.name, "sequence-b") == 0);
	assert(fd_readdir(&parent, (uint64_t)descriptor, &entry) == 1);
	assert(strcmp(entry.name, "sequence-c") == 0);
	assert(fd_readdir(&child, (uint64_t)descriptor, &entry) == 0);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	fd_table_destroy(&parent);
	fd_table_destroy(&child);

	fd_table_init_standard(&parent);
	assert(vfs_open("/tmp/phase8.txt", VFS_O_CREAT | VFS_O_WRONLY, 0640,
	    &file) == 0);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	assert(fd_write(&parent, (uint64_t)descriptor, new_data,
	    sizeof(new_data) - 1, &amount) == 0);
	assert(amount == sizeof(new_data) - 1);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	assert(vfs_stat_path("/tmp/phase8.txt", &status) == 0);
	assert(status.type == VNODE_REGULAR && status.size == sizeof(new_data) - 1);
	assert(vfs_open("/tmp/phase8.txt", VFS_O_RDONLY, 0, &file) == 0);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	memset(buffer, 0, sizeof(buffer));
	assert(fd_read(&parent, (uint64_t)descriptor, buffer, sizeof(buffer),
	    &amount) == 0);
	assert(amount == sizeof(new_data) - 1);
	assert(memcmp(buffer, new_data, amount) == 0);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	assert(vfs_open("/tmp/phase8.txt", VFS_O_RDONLY, 0, &file) == 0);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	assert(fd_write(&parent, (uint64_t)descriptor, "x", 1, &amount) == -9);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	assert(vfs_open("/tmp/phase8.txt", VFS_O_WRONLY | VFS_O_TRUNC, 0,
	    &file) == 0);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	assert(fd_set_offset(&parent, (uint64_t)descriptor, 5));
	assert(fd_write(&parent, (uint64_t)descriptor, "z", 1, &amount) == 0);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	assert(vfs_open("/tmp/phase8.txt", VFS_O_RDONLY, 0, &file) == 0);
	descriptor = fd_install(&parent, file);
	assert(descriptor >= 3);
	memset(buffer, 0xff, sizeof(buffer));
	assert(fd_read(&parent, (uint64_t)descriptor, buffer, 6, &amount) == 0);
	assert(amount == 6 && memcmp(buffer, "\0\0\0\0\0z", 6) == 0);
	assert(fd_close(&parent, (uint64_t)descriptor) == 0);
	assert(fd_close(&parent, 1) == 0);
	assert(fd_write(&parent, 1, "x", 1, &amount) == -9);
	fd_table_destroy(&parent);
}

int
main(int argc, char **argv)
{
	uint8_t *archive;
	size_t archive_size;

	assert(argc == 2);
	archive = read_archive(argv[1], &archive_size);
	test_archive_validation(archive, archive_size);
	test_vfs(archive, archive_size);
	free(archive);
	puts("VFS, RAMFS, newc initramfs, descriptors, and error tests passed");
	return 0;
}
