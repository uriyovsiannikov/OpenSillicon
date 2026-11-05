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
void process_wake_channel(void *channel) { (void)channel; }
bool terminal_input_available(void) { return false; }

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

	if (source == NULL) {
		int open_error = errno;
		fprintf(stderr, "could not open %s for %s: %s\n",
		    host_path, path, strerror(open_error));
		return -open_error;
	}
	error = vfs_create_path(path, VNODE_REGULAR, mode, &node);
	if (error != 0) {
		fprintf(stderr, "could not create %s: error %d\n", path, error);
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
				fprintf(stderr, "could not populate %s from %s at offset "
				    "%llu: error %d (wrote %zu of %zu bytes)\n",
				    path, host_path, (unsigned long long)offset,
				    error, written, amount);
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
	const char *libos_path = NULL;
	const char *tinyx_path = NULL;
	const char *startx_path = NULL;
	const char *twm_path = NULL;
	const char *xterm_path = NULL;
	const char *xeyes_path = NULL;
	const char *feh_path = NULL;
	const char *nano_path = NULL;
	const char *module_paths[32];
	size_t module_count = 0;
	int arg;

	if (argc < 4) {
		fprintf(stderr, "usage: %s image init-elf shell-elf [--libos elf] [--tinyx elf] [--startx elf] [--twm elf] [--xterm elf] [--xeyes elf] [--feh elf] [--nano elf] [--module file.ko] [--coreutils coreutils...]\n", argv[0]);
		return 2;
	}
	for (arg = 4; arg < argc; arg++) {
		if (strcmp(argv[arg], "--libos") == 0 && arg + 1 < argc) {
			if (libos_path != NULL) return 2;
			libos_path = argv[++arg];
		} else if (strcmp(argv[arg], "--tinyx") == 0 && arg + 1 < argc) {
			if (tinyx_path != NULL) return 2;
			tinyx_path = argv[++arg];
		} else if (strcmp(argv[arg], "--startx") == 0 && arg + 1 < argc) {
			if (startx_path != NULL) return 2;
			startx_path = argv[++arg];
		} else if (strcmp(argv[arg], "--twm") == 0 && arg + 1 < argc) {
			if (twm_path != NULL) return 2;
			twm_path = argv[++arg];
		} else if (strcmp(argv[arg], "--xterm") == 0 && arg + 1 < argc) {
			if (xterm_path != NULL) return 2;
			xterm_path = argv[++arg];
		} else if (strcmp(argv[arg], "--xeyes") == 0 && arg + 1 < argc) {
			if (xeyes_path != NULL) return 2;
			xeyes_path = argv[++arg];
		} else if (strcmp(argv[arg], "--feh") == 0 && arg + 1 < argc) {
			if (feh_path != NULL) return 2;
			feh_path = argv[++arg];
		} else if (strcmp(argv[arg], "--nano") == 0 && arg + 1 < argc) {
			if (nano_path != NULL) return 2;
			nano_path = argv[++arg];
		} else if (strcmp(argv[arg], "--module") == 0 && arg + 1 < argc) {
			if (module_count == 32) return 2;
			module_paths[module_count++] = argv[++arg];
		} else if (strcmp(argv[arg], "--coreutils") == 0) {
			if (coreutils_start != 0) return 2;
			coreutils_start = arg + 1;
			break;
		} else {
			fprintf(stderr, "unexpected argument: %s\n", argv[arg]);
			return 2;
		}
	}
	if (coreutils_start != 0)
		coreutils_count = argc - coreutils_start;
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
	    (error = add_directory("/var", 0755)) != 0 ||
	    (error = add_directory("/var/db", 0755)) != 0 ||
	    (error = add_directory("/var/db/pkg", 0755)) != 0 ||
	    (error = add_directory("/tmp", 01777)) != 0 ||
	    (error = add_file("/tmp/img1.jpg", "other/img1.jpg", 0644)) != 0 ||
	    (error = add_file("/sbin/init", argv[2], 0755)) != 0 ||
	    (error = add_file("/bin/sh", argv[3], 0755)) != 0) {
		fprintf(stderr, "SilliconFS population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (module_count != 0) {
		if ((error = add_directory("/boot", 0755)) != 0 ||
		    (error = add_directory("/boot/modules", 0755)) != 0) {
			fprintf(stderr, "module directory population failed: %d\n", error);
			fclose(image);
			return 1;
		}
		for (size_t i = 0; i < module_count; i++) {
			char destination[256];
			const char *name = strrchr(module_paths[i], '/');
			name = name == NULL ? module_paths[i] : name + 1;
			int length = snprintf(destination, sizeof(destination),
			    "/boot/modules/%s", name);
			if (*name == '\0' || strcmp(name, ".") == 0 ||
			    strcmp(name, "..") == 0 || length < 0 ||
			    (size_t)length >= sizeof(destination) ||
			    (error = add_file(destination, module_paths[i], 0644)) != 0) {
				fprintf(stderr, "module population failed: %s\n", module_paths[i]);
				fclose(image);
				return 1;
			}
		}
	}
	if (libos_path != NULL &&
	    (error = add_file("/bin/libos-test", libos_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS libos fixture population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (tinyx_path != NULL &&
	    (error = add_file("/bin/Xopensillicon", tinyx_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS TinyX population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (startx_path != NULL &&
	    (error = add_file("/bin/startx", startx_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS startx population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (twm_path != NULL &&
	    ((error = add_file("/bin/twm", twm_path, 0755)) != 0 ||
	    (error = add_file("/etc/twmrc",
	    "src/user/thirdparty/xorg/startx/system.twmrc", 0644)) != 0)) {
		fprintf(stderr, "SilliconFS twm population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (xterm_path != NULL &&
	    ((error = add_file("/bin/xterm", xterm_path, 0755)) != 0 ||
	    (error = add_directory("/usr", 0755)) != 0 ||
	    (error = add_directory("/usr/share", 0755)) != 0 ||
	    (error = add_directory("/usr/share/fonts", 0755)) != 0 ||
	    (error = add_directory("/usr/share/fonts/misc", 0755)) != 0 ||
	    (error = add_file("/usr/share/fonts/misc/gallant.bdf",
	    "src/user/thirdparty/xorg/fonts/gallant.bdf", 0644)) != 0 ||
	    (error = add_file("/usr/share/fonts/misc/fonts.dir",
	    "src/user/thirdparty/xorg/fonts/fonts.dir", 0644)) != 0)) {
		fprintf(stderr, "SilliconFS xterm population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (xeyes_path != NULL &&
	    (error = add_file("/bin/xeyes", xeyes_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS xeyes population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (feh_path != NULL &&
	    ((error = add_file("/bin/feh", feh_path, 0755)) != 0 ||
	    (xterm_path == NULL &&
	    (error = add_directory("/usr", 0755)) != 0) ||
	    (xterm_path == NULL &&
	    (error = add_directory("/usr/share", 0755)) != 0) ||
	    (error = add_directory("/usr/share/feh", 0755)) != 0 ||
	    (error = add_directory("/usr/share/feh/fonts", 0755)) != 0 ||
	    (error = add_directory("/usr/share/feh/images", 0755)) != 0 ||
	    (error = add_file("/usr/share/feh/fonts/yudit.ttf",
	    "src/user/thirdparty/xorg/feh/share/fonts/yudit.ttf", 0644)) != 0 ||
	    (error = add_file("/usr/share/feh/fonts/NotoSans-Medium.ttf",
	    "src/user/thirdparty/xorg/feh/share/fonts/NotoSans-Medium.ttf", 0644)) != 0 ||
	    (error = add_file("/usr/share/feh/images/feh.png",
	    "src/user/thirdparty/xorg/feh/share/images/feh.png", 0644)) != 0)) {
		fprintf(stderr, "SilliconFS feh population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (nano_path != NULL &&
	    (error = add_file("/bin/nano", nano_path, 0755)) != 0) {
		fprintf(stderr, "SilliconFS nano population failed: %d\n", error);
		fclose(image);
		return 1;
	}
	if (coreutils_start != 0) {
		static const char *names[] = {
			"ls", "cat", "echo", "pwd", "true", "false", "clear", "uname",
			"fetch", "head", "wc", "stat", "basename", "dirname",
			"kill", "sleep", "which", "ps", "netdiag", "ping",
			"poweroff", "reboot", "host", "ifconfig", "kldstat", "kldload", "kldunload",
			"pkg"
		};
		int index;
		if (coreutils_count !=
		    (int)(sizeof(names) / sizeof(names[0]))) {
			fprintf(stderr, "expected %zu coreutils ELF files, got %d\n",
			    sizeof(names) / sizeof(names[0]), coreutils_count);
			fclose(image);
			return 1;
		}
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
		static const char resolv_conf[] = "nameserver 10.15.3.3\n";
		if (add_data("/etc/passwd", passwd, sizeof(passwd) - 1, 0644) != 0 ||
		    add_data("/etc/group", group, sizeof(group) - 1, 0644) != 0 ||
		    add_data("/etc/resolv.conf", resolv_conf,
		    sizeof(resolv_conf) - 1, 0644) != 0) {
			fclose(image);
			return 1;
		}
	}
	if (fclose(image) != 0) return 1;
	return 0;
}
