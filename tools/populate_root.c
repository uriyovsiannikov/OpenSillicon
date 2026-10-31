#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/silliconfs.h"
#include "kernel/vfs.h"

struct image_context {
	FILE *file;
};

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *pointer) { free(pointer); return true; }
void klog(const char *message) { fputs(message, stderr); }
void klog_u64(uint64_t value) { fprintf(stderr, "%llu", (unsigned long long)value); }
_Noreturn void kernel_halt(void) { abort(); }
bool serial_write(const char *buffer, size_t length)
{ return fwrite(buffer, 1, length, stderr) == length; }
int tty_read(void *buffer, size_t length, size_t *read_length)
{ (void)buffer; (void)length; *read_length = 0; return -5; }
bool tty_write(const void *buffer, size_t length)
{ (void)buffer; (void)length; return true; }

static int
image_read(void *argument, uint32_t block, void *buffer)
{
	struct image_context *context = argument;
	if (fseek(context->file, (long)block * SILLICONFS_BLOCK_SIZE, SEEK_SET) != 0 ||
	    fread(buffer, 1, SILLICONFS_BLOCK_SIZE, context->file) !=
	    SILLICONFS_BLOCK_SIZE)
		return -EIO;
	return 0;
}

static int
image_write(void *argument, uint32_t block, const void *buffer)
{
	struct image_context *context = argument;
	if (fseek(context->file, (long)block * SILLICONFS_BLOCK_SIZE, SEEK_SET) != 0 ||
	    fwrite(buffer, 1, SILLICONFS_BLOCK_SIZE, context->file) !=
	    SILLICONFS_BLOCK_SIZE || fflush(context->file) != 0)
		return -EIO;
	return 0;
}

static int
add_directory(const char *path, uint32_t mode)
{
	struct vnode *node = NULL;
	int error = vfs_create_path(path, VNODE_DIRECTORY, mode, &node);
	if (error == 0)
		vnode_put(node);
	return error;
}

static int
add_file(const char *path, const char *host_path, uint32_t mode)
{
	FILE *source = fopen(host_path, "rb");
	struct vnode *node = NULL;
	unsigned char buffer[2048];
	uint64_t offset = 0;
	int error;

	if (source == NULL)
		return -errno;
	error = vfs_create_path(path, VNODE_REGULAR, mode, &node);
	if (error != 0) {
		fclose(source);
		return error;
	}
	for (;;) {
		size_t amount = fread(buffer, 1, sizeof(buffer), source);
		size_t written = 0;
		if (amount != 0) {
			error = node->operations->write(node, offset, buffer, amount,
			    &written);
			if (error != 0 || written != amount) {
				error = error != 0 ? error : -EIO;
				break;
			}
			offset += written;
		}
		if (amount < sizeof(buffer)) {
			if (ferror(source)) error = -EIO;
			break;
		}
	}
	vnode_put(node);
	fclose(source);
	return error;
}

static int
add_data(const char *path, const void *data, size_t length, uint32_t mode)
{
	struct vnode *node = NULL;
	size_t written = 0;
	int error = vfs_create_path(path, VNODE_REGULAR, mode, &node);
	if (error == 0) {
		error = node->operations->write(node, 0, data, length, &written);
		if (error == 0 && written != length)
			error = -EIO;
		vnode_put(node);
	}
	return error;
}

int
main(int argc, char **argv)
{
	struct image_context context;
	struct silliconfs_device device;
	struct vnode *root = NULL;
	FILE *image;
	int error;
	int coreutils_start = 0;
	int coreutils_count = 0;

	if (argc >= 6 && strcmp(argv[5], "--coreutils") == 0 && argc == 15) {
		coreutils_start = 6;
		coreutils_count = 9;
	} else if (argc >= 9 && strcmp(argv[8], "--coreutils") == 0 && argc == 18) {
		coreutils_start = 9;
		coreutils_count = 9;
	} else if (argc >= 6 && strcmp(argv[5], "--coreutils") == 0 && argc == 12) {
		coreutils_start = 6;
		coreutils_count = 6;
	} else if (argc >= 9 && strcmp(argv[8], "--coreutils") == 0 && argc == 17) {
		coreutils_start = 9;
		coreutils_count = 8;
	} else if (argc >= 6 && strcmp(argv[5], "--coreutils") == 0 && argc == 14) {
		coreutils_start = 6;
		coreutils_count = 8;
	}
	if (argc < 5 || (argc > 8 && coreutils_start == 0)) {
		fprintf(stderr, "usage: %s image init-elf shell-elf probe-elf [musl-hello-elf [musl-alloc-elf [musl-stdio-elf]]] [--coreutils ls cat echo pwd true false clear uname fetch]\n",
		    argv[0]);
		return 2;
	}
	image = fopen(argv[1], "r+b");
	if (image == NULL) {
		perror(argv[1]);
		return 1;
	}
	context.file = image;
	device = (struct silliconfs_device){
		.context = &context,
		.block_count = 0,
		.read_block = image_read,
		.write_block = image_write
	};
	if (fseek(image, 0, SEEK_END) != 0) return 1;
	device.block_count = (uint32_t)(ftell(image) / SILLICONFS_BLOCK_SIZE);
	if (!vfs_init() || silliconfs_mount(&device, &root) != 0 ||
	    vfs_mount("/", root) != 0) {
		fprintf(stderr, "could not mount formatted SilliconFS image\n");
		fclose(image);
		return 1;
	}
	vnode_put(root);
	if ((error = add_directory("/bin", 0755)) != 0 ||
	    (error = add_directory("/sbin", 0755)) != 0 ||
	    (error = add_directory("/etc", 0755)) != 0 ||
	    (error = add_directory("/tmp", 01777)) != 0 ||
	    (error = add_file("/sbin/init", argv[2], 0755)) != 0 ||
	    (error = add_file("/bin/sh", argv[3], 0755)) != 0 ||
	    (error = add_file("/bin/probe", argv[4], 0755)) != 0) {
		fprintf(stderr, "SilliconFS population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (argc >= 6 && strcmp(argv[5], "--coreutils") != 0 &&
	    (error = add_file("/bin/musl_hello", argv[5], 0755)) != 0) {
		fprintf(stderr, "SilliconFS musl test population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (argc >= 7 && strcmp(argv[5], "--coreutils") != 0 &&
	    (error = add_file("/bin/musl_alloc", argv[6], 0755)) != 0) {
		fprintf(stderr, "SilliconFS musl allocation test population failed: %d\n",
		    error);
		fclose(image);
		return 1;
	}
	if (argc >= 8 && strcmp(argv[5], "--coreutils") != 0 &&
	    (error = add_file("/bin/musl_stdio", argv[7], 0755)) != 0) {
		fprintf(stderr, "SilliconFS musl stdio test population failed: %d\n",
		    error);
		fclose(image);
		return 1;
	}
	if (coreutils_start != 0) {
		static const char *names[] = {
			"ls", "cat", "echo", "pwd", "true", "false", "clear", "uname",
			"fetch"
		};
		int index;
		for (index = 0; index < coreutils_count; index++) {
			char destination[32] = "/bin/";
			size_t position = 5;
			const char *name = names[index];
			while (*name != '\0') destination[position++] = *name++;
			destination[position] = '\0';
			if ((error = add_file(destination,
			    argv[coreutils_start + index], 0755)) != 0) {
				fprintf(stderr, "SilliconFS coreutils population failed: %d\n", error);
				fclose(image);
				return 1;
			}
		}
	}
	{
		static const char passwd[] =
		    "root:*:0:0:OpenSillicon root:/:/bin/sh\n"
		    "user:*:1000:1000:OpenSillicon user:/tmp:/bin/sh\n";
		static const char group[] = "root:*:0:\nuser:*:1000:\n";
		if (add_data("/etc/passwd", passwd, sizeof(passwd) - 1, 0644) != 0 ||
		    add_data("/etc/group", group, sizeof(group) - 1, 0644) != 0) {
			fclose(image);
			return 1;
		}
	}
	if (fclose(image) != 0) return 1;
	return 0;
}
