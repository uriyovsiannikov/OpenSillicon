#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/devfs.h"
#include "kernel/fd.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/process.h"
#include "kernel/procfs.h"
#include "kernel/pmm.h"
#include "kernel/timer.h"
#include "kernel/terminal.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

static struct process_snapshot_entry test_process = {
	.pid = 42,
	.parent_pid = 7,
	.state = 1,
	.exit_status = 0
};
static int test_process_error;
static uint64_t test_ticks;
static uint64_t test_free_pages = 128;

uint64_t pmm_usable_pages(void) { return 256; }
uint64_t pmm_free_pages(void) { return test_free_pages; }

static char console_output[64];
static size_t console_output_length;
static const char console_input[] = "tty input";
static size_t console_input_offset;

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
	(void)buffer;
	(void)length;
	return true;
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

int
tty_read(void *buffer, size_t length, size_t *read_length)
{
	size_t remaining = sizeof(console_input) - 1U - console_input_offset;
	size_t amount = length < remaining ? length : remaining;

	if (read_length == NULL || (buffer == NULL && length != 0))
		return -22;
	if (amount != 0)
		memcpy(buffer, console_input + console_input_offset, amount);
	console_input_offset += amount;
	*read_length = amount;
	return 0;
}

bool
tty_write(const void *buffer, size_t length)
{
	if ((buffer == NULL && length != 0) ||
	    length > sizeof(console_output) - console_output_length)
		return false;
	if (length != 0)
		memcpy(console_output + console_output_length, buffer, length);
	console_output_length += length;
	return true;
}

bool terminal_input_available(void) { return false; }
void process_wake_channel(void *channel) { (void)channel; }

int
process_current_snapshot(struct process_snapshot_entry *entry)
{
	if (test_process_error != 0)
		return test_process_error;
	if (entry == NULL)
		return -22;
	*entry = test_process;
	return 0;
}

uint64_t
timer_ticks(void)
{
	return test_ticks;
}

static int
open_fd(struct fd_table *table, const char *path, uint32_t flags)
{
	struct fd_object *object;
	int descriptor;

	assert(vfs_open(path, flags, 0, &object) == 0);
	descriptor = fd_install(table, object);
	assert(descriptor >= 3);
	return descriptor;
}

static size_t
read_to_end(struct fd_table *table, int descriptor, char *buffer, size_t size)
{
	size_t total = 0;

	while (total < size) {
		size_t amount = 0;

		assert(fd_read(table, (uint64_t)descriptor, buffer + total,
		    size - total, &amount) == 0);
		if (amount == 0)
			break;
		total += amount;
	}
	return total;
}

static bool
contains(const char *buffer, size_t length, const char *needle)
{
	size_t needle_length = strlen(needle);

	if (needle_length > length)
		return false;
	for (size_t i = 0; i <= length - needle_length; i++)
		if (memcmp(buffer + i, needle, needle_length) == 0)
			return true;
	return false;
}

static void
test_devfs(void)
{
	static const char expected_names[][8] = { "console", "null", "zero" };
	struct fd_table table;
	struct fd_metadata metadata;
	struct vfs_dirent entry;
	char buffer[32];
	size_t amount;
	int directory;
	int descriptor;

	fd_table_init_standard(&table);
	directory = open_fd(&table, "/dev", VFS_O_RDONLY | VFS_O_DIRECTORY);
	for (size_t i = 0; i < 3; i++) {
		assert(fd_readdir(&table, (uint64_t)directory, &entry) == 1);
		assert(strcmp(entry.name, expected_names[i]) == 0);
		assert(entry.type == VNODE_DEVICE);
	}
	assert(fd_readdir(&table, (uint64_t)directory, &entry) == 0);
	assert(fd_close(&table, (uint64_t)directory) == 0);

	assert(vfs_open("/dev/missing", VFS_O_RDONLY, 0,
	    &(struct fd_object *){0}) == -2);
	descriptor = open_fd(&table, "/dev/null", VFS_O_RDWR);
	assert(fd_object_stat(fd_object_for_descriptor(&table,
	    (uint64_t)descriptor), &metadata) == 0);
	assert(metadata.type == VNODE_DEVICE);
	assert(fd_read(&table, (uint64_t)descriptor, buffer, sizeof(buffer),
	    &amount) == 0 && amount == 0);
	assert(fd_write(&table, (uint64_t)descriptor, "discard", 7,
	    &amount) == 0 && amount == 7);
	assert(fd_seek(&table, (uint64_t)descriptor, 0, 0, &amount) == -29);
	assert(fd_close(&table, (uint64_t)descriptor) == 0);

	descriptor = open_fd(&table, "/dev/zero", VFS_O_RDWR);
	memset(buffer, 0xff, sizeof(buffer));
	assert(fd_read(&table, (uint64_t)descriptor, buffer, 5, &amount) == 0);
	assert(amount == 5 && memcmp(buffer, "\0\0\0\0\0", 5) == 0);
	memset(buffer, 0xff, sizeof(buffer));
	assert(fd_read(&table, (uint64_t)descriptor, buffer, 4, &amount) == 0);
	assert(amount == 4 && memcmp(buffer, "\0\0\0\0", 4) == 0);
	assert(fd_write(&table, (uint64_t)descriptor, "ignored", 7,
	    &amount) == 0 && amount == 7);
	assert(fd_seek(&table, (uint64_t)descriptor, 0, 0, &amount) == -29);
	assert(fd_close(&table, (uint64_t)descriptor) == 0);

	descriptor = open_fd(&table, "/dev/console", VFS_O_RDWR);
	memset(buffer, 0, sizeof(buffer));
	assert(fd_read(&table, (uint64_t)descriptor, buffer, 4, &amount) == 0);
	assert(amount == 4 && memcmp(buffer, "tty ", 4) == 0);
	assert(fd_write(&table, (uint64_t)descriptor, "out", 3,
	    &amount) == 0 && amount == 3);
	assert(console_output_length == 3 && memcmp(console_output, "out", 3) == 0);
	assert(fd_close(&table, (uint64_t)descriptor) == 0);
	fd_table_destroy(&table);
}

static void
test_procfs(void)
{
	struct fd_table table;
	struct vfs_dirent entry;
	struct vnode *lookup_node;
	char content[128];
	size_t length;
	size_t amount;
	int directory;
	int descriptor;
	uint64_t self_inode;

	fd_table_init_standard(&table);
	test_ticks = 12345;
	directory = open_fd(&table, "/proc", VFS_O_RDONLY | VFS_O_DIRECTORY);
	assert(fd_readdir(&table, (uint64_t)directory, &entry) == 1);
	assert(strcmp(entry.name, "self") == 0 && entry.type == VNODE_REGULAR);
	self_inode = entry.inode;
	assert(vfs_lookup("/proc/self", &lookup_node) == 0);
	assert(lookup_node->inode == self_inode);
	vnode_put(lookup_node);
	assert(fd_readdir(&table, (uint64_t)directory, &entry) == 1);
	assert(strcmp(entry.name, "uptime") == 0 && entry.type == VNODE_REGULAR);
	assert(fd_readdir(&table, (uint64_t)directory, &entry) == 1);
	assert(strcmp(entry.name, "meminfo") == 0 && entry.type == VNODE_REGULAR);
	assert(vfs_lookup("/proc/meminfo", &lookup_node) == 0);
	assert(lookup_node->inode == entry.inode);
	vnode_put(lookup_node);
	assert(fd_readdir(&table, (uint64_t)directory, &entry) == 0);
	assert(fd_close(&table, (uint64_t)directory) == 0);

	descriptor = open_fd(&table, "/proc/uptime", VFS_O_RDONLY);
	assert(fd_read(&table, (uint64_t)descriptor, content, 2, &amount) == 0);
	assert(amount == 2 && memcmp(content, "12", 2) == 0);
	length = read_to_end(&table, descriptor, content + 2,
	    sizeof(content) - 2);
	assert(length == 5 && memcmp(content, "123.45\n", 7) == 0);
	assert(fd_read(&table, (uint64_t)descriptor, content, sizeof(content),
	    &amount) == 0 && amount == 0);
	assert(fd_seek(&table, (uint64_t)descriptor, 0, 0, &amount) == 0 &&
	    amount == 0);
	assert(fd_close(&table, (uint64_t)descriptor) == 0);

	descriptor = open_fd(&table, "/proc/meminfo", VFS_O_RDONLY);
	assert(fd_read(&table, (uint64_t)descriptor, content, 3, &amount) == 0);
	assert(amount == 3);
	test_free_pages = 64;
	length = read_to_end(&table, descriptor, content + 3, sizeof(content) - 3);
	assert(length + 3 == strlen("MemTotal: 1024 kB\nMemFree: 512 kB\n"));
	assert(memcmp(content, "MemTotal: 1024 kB\nMemFree: 512 kB\n", length + 3) == 0);
	assert(fd_read(&table, (uint64_t)descriptor, content, sizeof(content),
	    &amount) == 0 && amount == 0);
	assert(fd_close(&table, (uint64_t)descriptor) == 0);
	descriptor = open_fd(&table, "/proc/meminfo", VFS_O_RDONLY);
	length = read_to_end(&table, descriptor, content, sizeof(content));
	assert(contains(content, length, "MemFree: 256 kB\n"));
	assert(fd_close(&table, (uint64_t)descriptor) == 0);
	assert(vfs_open("/proc/meminfo", VFS_O_WRONLY, 0,
	    &(struct fd_object *){0}) == -30);

	descriptor = open_fd(&table, "/proc/self", VFS_O_RDONLY);
	length = read_to_end(&table, descriptor, content, sizeof(content));
	assert(length == strlen("Pid:\t42\nPPid:\t7\nState:\trunning\nExitStatus:\t0\n"));
	assert(memcmp(content,
	    "Pid:\t42\nPPid:\t7\nState:\trunning\nExitStatus:\t0\n",
	    length) == 0);
	assert(fd_close(&table, (uint64_t)descriptor) == 0);

	test_process.pid = 99;
	test_process.state = 0;
	descriptor = open_fd(&table, "/proc/self", VFS_O_RDONLY);
	length = read_to_end(&table, descriptor, content, sizeof(content));
	assert(contains(content, length, "Pid:\t99"));
	assert(contains(content, length, "State:\tready"));
	assert(fd_close(&table, (uint64_t)descriptor) == 0);

	assert(vfs_open("/proc/missing", VFS_O_RDONLY, 0,
	    &(struct fd_object *){0}) == -2);
	assert(vfs_open("/proc/self/child", VFS_O_RDONLY, 0,
	    &(struct fd_object *){0}) == -20);
	assert(vfs_open("/proc/self", VFS_O_WRONLY, 0,
	    &(struct fd_object *){0}) == -30);
	assert(procfs_mount("/proc") == -16);
	assert(devfs_mount("/proc") == -16);

	test_process_error = -3;
	assert(vfs_open("/proc/self", VFS_O_RDONLY, 0,
	    &(struct fd_object *){0}) == -3);
	test_process_error = 0;
	fd_table_destroy(&table);
}

int
main(void)
{
	struct vnode *root;
	struct vnode *directory;
	struct vnode *file;

	assert(vfs_init());
	root = ramfs_create_root();
	assert(root != NULL && vfs_mount("/", root) == 0);
	vnode_put(root);
	assert(vfs_create_path("/dev", VNODE_DIRECTORY, 0755, &directory) == 0);
	vnode_put(directory);
	assert(vfs_create_path("/proc", VNODE_DIRECTORY, 0755, &directory) == 0);
	vnode_put(directory);
	assert(vfs_create_path("/busy", VNODE_REGULAR, 0644, &file) == 0);
	vnode_put(file);
	assert(devfs_mount("/busy") == -20);
	assert(procfs_mount("/missing") == -2);
	assert(devfs_mount("/dev") == 0);
	assert(procfs_mount("/proc") == 0);
	test_devfs();
	test_procfs();
	puts("devfs/procfs VFS, device I/O, snapshots, offsets, and mount tests passed");
	return 0;
}
