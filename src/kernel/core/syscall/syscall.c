#include "kernel/pty.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/user.h"
#include "kernel/fd.h"
#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/module.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/tty.h"
#include "kernel/vmm.h"
#include "kernel/vfs.h"
#include "kernel/network_service.h"
#include "kernel/cyclone_net.h"
#include "kernel/udp_socket.h"
#include "kernel/tcp_socket.h"
#include "kernel/icmp_echo.h"
#include "kernel/unix_socket.h"
#include "kernel/timer.h"
#include "kernel/framebuffer_device.h"
#include "abi/framebuffer.h"
#include "abi/input.h"
#include "abi/select.h"
#include "abi/ioctl.h"
#include "arch/x86_64/ps2_keyboard.h"

#define SYSCALL_VECTOR 128U
#define SYSCALL_MAX_IO 4096U
#define SYSCALL_MAX_IOV 16U
#define SYSCALL_MAX_IOV_BYTES (64U * 1024U)
#define SYSCALL_IOV_CHUNK 512U
#define TIOCGWINSZ_REQUEST 0x5413U
#define OS_FIONBIO 0x5421U
#define TCGETS_REQUEST 0x5401U
#define TCSETS_REQUEST 0x5402U
#define TIOCSRAW_REQUEST 0x4f530001U
#define TIOCCANON_REQUEST 0x4f530002U
#define TIOCSFB_REQUEST 0x4f530003U
#define ERRNO_EFAULT 14U
#define ERRNO_ESRCH 3U
#define ERRNO_EINVAL 22U
#define ERRNO_E2BIG 7U
#define ERRNO_ENOSYS 78U
#define ERRNO_ECHILD 10U
#define ERRNO_EAGAIN 11U
#define ERRNO_ENOMEM 12U
#define ERRNO_ENAMETOOLONG 36U
#define ERRNO_EPIPE 32U
#define ERRNO_EMFILE 24U
#define ERRNO_ENOTTY 25U
#define ERRNO_ENODEV 19U
#define CLOCK_MONOTONIC_ID 1U
#define MONOTONIC_TICK_HZ 100U
#define MONOTONIC_TICK_NS (1000000000ULL / MONOTONIC_TICK_HZ)
#define NATIVE_S_IFREG 0100000U
#define NATIVE_S_IFDIR 0040000U
#define NATIVE_S_IFCHR 0020000U
#define NATIVE_AT_FDCWD (-100)
#define NATIVE_AT_SYMLINK_NOFOLLOW 0x100U
#define NATIVE_F_GETFD 1U
#define NATIVE_F_SETFD 2U
#define NATIVE_F_GETFL 3U
#define NATIVE_F_SETFL 4U
#define NATIVE_F_SETOWN 8U
#define NATIVE_O_NONBLOCK FD_STATUS_NONBLOCK
#define NATIVE_O_ACCMODE 3U

struct native_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

/* musl 1.2.6 x86_64 arch/x86_64/kstat.h, also layout-compatible with its
 * public struct stat: timestamps are stored as sec/nsec pairs. */
struct native_kstat {
	uint64_t st_dev;
	uint64_t st_ino;
	uint64_t st_nlink;
	uint32_t st_mode;
	uint32_t st_uid;
	uint32_t st_gid;
	uint32_t pad0;
	uint64_t st_rdev;
	int64_t st_size;
	int64_t st_blksize;
	int64_t st_blocks;
	int64_t st_atime_sec;
	int64_t st_atime_nsec;
	int64_t st_mtime_sec;
	int64_t st_mtime_nsec;
	int64_t st_ctime_sec;
	int64_t st_ctime_nsec;
	int64_t unused[3];
};

_Static_assert(sizeof(struct native_kstat) == 144,
	"musl x86_64 kstat ABI size mismatch");
_Static_assert(offsetof(struct native_kstat, st_size) == 48,
	"musl x86_64 kstat st_size offset mismatch");
_Static_assert(offsetof(struct native_kstat, st_atime_sec) == 72,
	"musl x86_64 kstat timestamp offset mismatch");

static int
native_kstat_from_metadata(struct native_kstat *value,
	const struct fd_metadata *metadata)
{
	if (value == NULL || metadata == NULL)
		return -EINVAL;
	if (metadata->size > INT64_MAX)
		return -EOVERFLOW;
	*value = (struct native_kstat){0};
	value->st_ino = metadata->inode;
	value->st_size = (int64_t)metadata->size;
	value->st_mode = metadata->mode & 07777U;
	value->st_uid = metadata->uid;
	value->st_gid = metadata->gid;
	if (metadata->type == VNODE_REGULAR)
		value->st_mode |= NATIVE_S_IFREG;
	else if (metadata->type == VNODE_DIRECTORY)
		value->st_mode |= NATIVE_S_IFDIR;
	else if (metadata->type == VNODE_DEVICE)
		value->st_mode |= NATIVE_S_IFCHR;
	else
		return -EOPNOTSUPP;
	return 0;
}

struct syscall_iovec {
	uint64_t base;
	uint64_t length;
};

struct syscall_winsize {
	uint16_t rows;
	uint16_t columns;
	uint16_t xpixel;
	uint16_t ypixel;
};

static struct fd_table *descriptor_table;
static bool syscall_handler_registered;

static struct fd_table *
current_fd_table(void)
{
	struct fd_table *table = process_current_fd_table();

	return table == NULL ? descriptor_table : table;
}

static uint64_t
errno_from_kernel(int error)
{
	return error < 0 ? (uint64_t)(-error) : ERRNO_EINVAL;
}

static uint64_t
errno_from_module(enum kernel_module_error error)
{
	switch (error) {
	case KERNEL_MODULE_OK: return 0;
	case KERNEL_MODULE_NO_MEMORY: return 12U;
	case KERNEL_MODULE_ALREADY_LOADED: return 17U;
	case KERNEL_MODULE_NOT_FOUND: return 2U;
	case KERNEL_MODULE_BUSY: return 16U;
	case KERNEL_MODULE_INIT_FAILED: return 6U;
	case KERNEL_MODULE_IO_ERROR: return 5U;
	case KERNEL_MODULE_UNSUPPORTED:
	case KERNEL_MODULE_UNSUPPORTED_RELOCATION: return 95U;
	default: return ERRNO_EINVAL;
	}
}

static int
copy_user_string(uint64_t address, char *destination, size_t capacity)
{
	struct address_space *space = vmm_active_address_space();
	size_t i;

	if (destination == NULL || capacity == 0)
		return -ERRNO_EFAULT;
	for (i = 0; i < capacity; i++) {
		if (address > UINT64_MAX - i ||
		    !vmm_copy_from_user(space, &destination[i], address + i, 1))
			return -ERRNO_EFAULT;
		if (destination[i] == '\0')
			return 0;
	}
	return -(int)ERRNO_ENAMETOOLONG;
}

static void
syscall_error(struct interrupt_frame *frame, uint64_t error)
{
	frame->rax = error;
	frame->rflags |= 1U;
}

static bool
fd_set_contains(const struct os_fd_set *set, unsigned int descriptor)
{
	return set != NULL && (set->words[descriptor / 64U] &
	    (1ULL << (descriptor % 64U))) != 0;
}

static void
fd_set_add(struct os_fd_set *set, unsigned int descriptor)
{
	set->words[descriptor / 64U] |= 1ULL << (descriptor % 64U);
}

static struct interrupt_frame *
syscall_select(struct interrupt_frame *frame)
{
	struct os_fd_set requested_read = {0}, requested_write = {0};
	struct os_fd_set requested_except = {0};
	struct os_fd_set ready_read = {0}, ready_write = {0};
	struct os_fd_set ready_except = {0};
	struct os_timeval timeout;
	uint64_t nfds = frame->rdi;
	uint64_t deadline = process_select_deadline();
	uint64_t now;
	uint64_t wait_ticks = 0;
	int ready_count = 0;
	bool has_timeout = frame->r8 != 0;
	if (nfds > FD_TABLE_CAPACITY) {
		process_select_deadline_clear();
		syscall_error(frame, EINVAL);
		return frame;
	}
	if ((frame->rsi != 0 && !vmm_copy_from_user(vmm_active_address_space(),
	    &requested_read, frame->rsi, sizeof(requested_read))) ||
	    (frame->rdx != 0 && !vmm_copy_from_user(vmm_active_address_space(),
	    &requested_write, frame->rdx, sizeof(requested_write))) ||
	    (frame->r10 != 0 && !vmm_copy_from_user(vmm_active_address_space(),
	    &requested_except, frame->r10, sizeof(requested_except)))) {
		process_select_deadline_clear();
		syscall_error(frame, EFAULT);
		return frame;
	}
	if (has_timeout && !vmm_copy_from_user(vmm_active_address_space(),
	    &timeout, frame->r8, sizeof(timeout))) {
		process_select_deadline_clear();
		syscall_error(frame, EFAULT);
		return frame;
	}
	if (has_timeout && (timeout.seconds < 0 || timeout.microseconds < 0 ||
	    timeout.microseconds >= 1000000)) {
		process_select_deadline_clear();
		syscall_error(frame, EINVAL);
		return frame;
	}
	if ((frame->rsi != 0 && !vmm_validate_user_write(
	    vmm_active_address_space(), frame->rsi, sizeof(ready_read))) ||
	    (frame->rdx != 0 && !vmm_validate_user_write(
	    vmm_active_address_space(), frame->rdx, sizeof(ready_write))) ||
	    (frame->r10 != 0 && !vmm_validate_user_write(
	    vmm_active_address_space(), frame->r10, sizeof(ready_except)))) {
		process_select_deadline_clear();
		syscall_error(frame, EFAULT);
		return frame;
	}
	for (unsigned int descriptor = 0; descriptor < nfds; descriptor++) {
		uint32_t requested = 0, ready;
		struct fd_object *object;
		if (fd_set_contains(&requested_read, descriptor))
			requested |= FD_POLL_READ;
		if (fd_set_contains(&requested_write, descriptor))
			requested |= FD_POLL_WRITE;
		if (fd_set_contains(&requested_except, descriptor))
			requested |= FD_POLL_ERROR;
		if (requested == 0)
			continue;
		object = fd_object_for_descriptor(current_fd_table(), descriptor);
		if (object == NULL) {
			process_select_deadline_clear();
			syscall_error(frame, EBADF);
			return frame;
		}
		ready = fd_object_poll(object, requested);
		if (ready != 0)
			ready_count++;
		if ((ready & FD_POLL_READ) != 0) {
			fd_set_add(&ready_read, descriptor);
		}
		if ((ready & FD_POLL_WRITE) != 0) {
			fd_set_add(&ready_write, descriptor);
		}
		if ((ready & FD_POLL_ERROR) != 0) {
			fd_set_add(&ready_except, descriptor);
		}
	}
	if (ready_count != 0) {
		process_select_deadline_clear();
		if ((frame->rsi != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rsi, &ready_read, sizeof(ready_read))) ||
		    (frame->rdx != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rdx, &ready_write, sizeof(ready_write))) ||
		    (frame->r10 != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->r10, &ready_except, sizeof(ready_except)))) {
			syscall_error(frame, EFAULT);
			return frame;
		}
		frame->rax = (uint64_t)ready_count;
		return frame;
	}
	if (deadline == 0) {
		if (!has_timeout) {
			deadline = UINT64_MAX;
		} else {
			if ((uint64_t)timeout.seconds >
			    (UINT64_MAX - MONOTONIC_TICK_HZ) / MONOTONIC_TICK_HZ) {
				process_select_deadline_clear();
				syscall_error(frame, EINVAL);
				return frame;
			}
			wait_ticks = (uint64_t)timeout.seconds *
			    MONOTONIC_TICK_HZ;
			wait_ticks += ((uint64_t)timeout.microseconds *
			    MONOTONIC_TICK_HZ + 999999U) / 1000000U;
			now = timer_ticks();
			if (wait_ticks > UINT64_MAX - now) {
				process_select_deadline_clear();
				syscall_error(frame, EINVAL);
				return frame;
			}
			deadline = now + wait_ticks;
		}
		process_select_deadline_set(deadline);
	}
	if (deadline != UINT64_MAX && timer_ticks() >= deadline) {
		process_select_deadline_clear();
		if ((frame->rsi != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rsi, &ready_read, sizeof(ready_read))) ||
		    (frame->rdx != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->rdx, &ready_write, sizeof(ready_write))) ||
		    (frame->r10 != 0 && !vmm_copy_to_user(vmm_active_address_space(),
		    frame->r10, &ready_except, sizeof(ready_except)))) {
			syscall_error(frame, EFAULT);
			return frame;
		}
		frame->rax = 0;
		return frame;
	}
	return process_block_on_channel_until(frame, fd_readiness_wait_channel(),
	    deadline == UINT64_MAX ? 0 : deadline);
}

static int
load_iovecs(struct interrupt_frame *frame, struct syscall_iovec *vectors,
	uint64_t *total)
{
	struct address_space *space = vmm_active_address_space();
	uint64_t count = frame->rdx;
	uint64_t total_bytes = 0;
	uint64_t i;

	if (count > SYSCALL_MAX_IOV)
		return -(int)ERRNO_EINVAL;
	if (count != 0 && (frame->rsi > UINT64_MAX -
	    count * sizeof(*vectors) || !vmm_copy_from_user(space, vectors,
	    frame->rsi, (size_t)(count * sizeof(*vectors)))))
		return -(int)ERRNO_EFAULT;
	for (i = 0; i < count; i++) {
		if (vectors[i].length > SYSCALL_MAX_IOV_BYTES - total_bytes)
			return -(int)ERRNO_EINVAL;
		total_bytes += vectors[i].length;
		if (vectors[i].length != 0 &&
		    vectors[i].base > UINT64_MAX - vectors[i].length)
			return -(int)ERRNO_EFAULT;
	}
	*total = total_bytes;
	return 0;
}

static int
validate_iovec_buffers(const struct syscall_iovec *vectors, uint64_t count,
	bool for_read)
{
	struct address_space *space = vmm_active_address_space();
	char scratch[SYSCALL_IOV_CHUNK];
	uint64_t i;

	for (i = 0; i < count; i++) {
		uint64_t offset = 0;

		if (vectors[i].length == 0)
			continue;
		if (for_read) {
			if (!vmm_validate_user_write(space, vectors[i].base,
			    (size_t)vectors[i].length))
				return -(int)ERRNO_EFAULT;
			continue;
		}
		while (offset < vectors[i].length) {
			size_t amount = (size_t)(vectors[i].length - offset);

			if (amount > sizeof(scratch))
				amount = sizeof(scratch);
			if (!vmm_copy_from_user(space, scratch,
			    vectors[i].base + offset, amount))
				return -(int)ERRNO_EFAULT;
			offset += amount;
		}
	}
	return 0;
}

static struct interrupt_frame *
syscall_writev(struct interrupt_frame *frame)
{
	struct syscall_iovec vectors[SYSCALL_MAX_IOV];
	char buffer[SYSCALL_IOV_CHUNK];
	struct fd_table *table = current_fd_table();
	struct fd_object *object = fd_object_for_descriptor(table, frame->rdi);
	uint64_t count = frame->rdx;
	uint64_t total = 0;
	uint64_t written = 0;
	uint64_t i;
	int result;

	if (object == NULL || object->operations == NULL ||
	    object->operations->write == NULL) {
		syscall_error(frame, 9);
		return frame;
	}
	result = load_iovecs(frame, vectors, &total);
	if (result == 0)
		result = validate_iovec_buffers(vectors, count, false);
	if (result != 0) {
		syscall_error(frame, errno_from_kernel(result));
		return frame;
	}
	for (i = 0; i < count; i++) {
		uint64_t offset = 0;

		while (offset < vectors[i].length) {
			size_t chunk = (size_t)(vectors[i].length - offset);
			size_t amount = 0;

			if (chunk > sizeof(buffer))
				chunk = sizeof(buffer);
			if (!vmm_copy_from_user(vmm_active_address_space(), buffer,
			    vectors[i].base + offset, chunk)) {
				if (written != 0)
					goto partial;
				syscall_error(frame, ERRNO_EFAULT);
				return frame;
			}
			if (pipe_is_write_endpoint(object)) {
				result = pipe_write_try(object, buffer, chunk, &amount);
				if (result == -(int)ERRNO_EAGAIN) {
					if (written != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_on_channel(frame,
					    pipe_wait_channel(object, PIPE_WRITE_WAIT));
				}
			} else {
				result = fd_write(table, frame->rdi, buffer, chunk,
				    &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    (unix_socket_is_object(object) || pty_is_object(object) ||
				    tcp_socket_is_object(object))) {
					if (written != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_on_channel(frame,
					    unix_socket_is_object(object) ?
					    unix_socket_wait_channel(object, false) :
					    fd_readiness_wait_channel());
				}
			}
			if (result != 0) {
				if (written != 0)
					goto partial;
				syscall_error(frame, errno_from_kernel(result));
				return frame;
			}
			written += amount;
			offset += amount;
			if (amount < chunk)
				goto partial;
		}
	}
partial:
	(void)total;
	frame->rax = written;
	return frame;
}

static struct interrupt_frame *
syscall_readv(struct interrupt_frame *frame)
{
	struct syscall_iovec vectors[SYSCALL_MAX_IOV];
	char buffer[SYSCALL_IOV_CHUNK];
	struct fd_table *table = current_fd_table();
	struct fd_object *object = fd_object_for_descriptor(table, frame->rdi);
	uint64_t count = frame->rdx;
	uint64_t total = 0;
	uint64_t received = 0;
	uint64_t i;
	int result;

	if (object == NULL || object->operations == NULL ||
	    object->operations->read == NULL) {
		syscall_error(frame, 9);
		return frame;
	}
	result = load_iovecs(frame, vectors, &total);
	if (result == 0)
		result = validate_iovec_buffers(vectors, count, true);
	if (result != 0) {
		syscall_error(frame, errno_from_kernel(result));
		return frame;
	}
	for (i = 0; i < count; i++) {
		uint64_t offset = 0;

		while (offset < vectors[i].length) {
			size_t chunk = (size_t)(vectors[i].length - offset);
			size_t amount = 0;
			uint64_t old_offset = fd_get_offset(table, frame->rdi);

			if (chunk > sizeof(buffer))
				chunk = sizeof(buffer);
			/* TTY reads may sleep until more input arrives. Once one
			 * fragment has been delivered, return it rather than sleeping
			 * for a later iovec. */
			if (received != 0 && fd_object_is_terminal(object))
				goto partial;
			if (pipe_is_read_endpoint(object)) {
				result = pipe_read_stage(object, buffer, chunk, &amount);
				if (result == -(int)ERRNO_EAGAIN) {
					if (received != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_on_channel(frame,
					    pipe_wait_channel(object, PIPE_READ_WAIT));
				}
			} else {
				result = fd_read(table, frame->rdi, buffer, chunk,
				    &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    (unix_socket_is_object(object) || pty_is_object(object) ||
				    tcp_socket_is_object(object))) {
					if (received != 0)
						goto partial;
					if (fd_object_nonblocking(object)) {
						syscall_error(frame, ERRNO_EAGAIN);
						return frame;
					}
					return process_block_on_channel(frame,
					    unix_socket_is_object(object) ?
					    unix_socket_wait_channel(object, true) :
					    fd_readiness_wait_channel());
				}
			}
			if (result != 0) {
				if (received != 0)
					goto partial;
				syscall_error(frame, errno_from_kernel(result));
				return frame;
			}
			if (amount != 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), vectors[i].base + offset,
			    buffer, amount)) {
				if (!pipe_is_read_endpoint(object))
					(void)fd_set_offset(table, frame->rdi, old_offset);
				if (received != 0)
					goto partial;
				syscall_error(frame, ERRNO_EFAULT);
				return frame;
			}
			if (pipe_is_read_endpoint(object) &&
			    pipe_read_commit(object, amount) != 0) {
				if (received != 0)
					goto partial;
				syscall_error(frame, ERRNO_EINVAL);
				return frame;
			}
			received += amount;
			offset += amount;
			if (amount < chunk)
				goto partial;
		}
	}
partial:
	(void)total;
	frame->rax = received;
	return frame;
}

struct native_dirent_record {
	uint64_t inode;
	int64_t offset;
	uint16_t record_length;
	uint8_t type;
	char name[];
};

_Static_assert(offsetof(struct native_dirent_record, name) == 19,
	"native getdents record header ABI mismatch");

#define NATIVE_DT_DIR 4U
#define NATIVE_DT_REG 8U
#define NATIVE_DT_CHR 2U
#define SYSCALL_GETDENTS_MAX 4096U
#define SYSCALL_EIO 5U
#define SYSCALL_EBADF 9U

static struct interrupt_frame *
syscall_getdents(struct interrupt_frame *frame)
{
	uint64_t records[SYSCALL_GETDENTS_MAX / sizeof(uint64_t)];
	char *output = (char *)records;
	struct fd_table *table = current_fd_table();
	uint64_t descriptor = frame->rdi;
	uint64_t start_offset;
	size_t capacity;
	size_t written = 0;
	int result;

	if (fd_object_for_descriptor(table, descriptor) == NULL) {
		syscall_error(frame, SYSCALL_EBADF);
		return frame;
	}
	if (frame->rdx == 0 || frame->rdx > SYSCALL_GETDENTS_MAX) {
		syscall_error(frame, ERRNO_EINVAL);
		return frame;
	}
	capacity = (size_t)frame->rdx;
	if (!vmm_validate_user_write(vmm_active_address_space(), frame->rsi,
	    capacity)) {
		syscall_error(frame, ERRNO_EFAULT);
		return frame;
	}
	start_offset = fd_get_offset(table, descriptor);

	while (written < capacity) {
		struct vfs_dirent entry;
		struct native_dirent_record *record;
		uint64_t before = fd_get_offset(table, descriptor);
		uint64_t after;
		size_t name_length = 0;
		size_t record_length;
		size_t index;
		uint8_t type;

		result = fd_readdir(table, descriptor, &entry);
		if (result < 0) {
			if (written == 0)
				syscall_error(frame, errno_from_kernel(result));
			break;
		}
		if (result == 0)
			break;
		after = fd_get_offset(table, descriptor);
		while (name_length < sizeof(entry.name) &&
		    entry.name[name_length] != '\0')
			name_length++;
		if (before == UINT64_MAX || after != before + 1U || after > INT64_MAX ||
		    entry.inode == 0 || name_length == 0 ||
		    name_length == sizeof(entry.name)) {
			(void)fd_set_offset(table, descriptor, before);
			if (written == 0)
				syscall_error(frame, SYSCALL_EIO);
			break;
		}
		if (entry.type == VNODE_DIRECTORY)
			type = NATIVE_DT_DIR;
		else if (entry.type == VNODE_REGULAR)
			type = NATIVE_DT_REG;
		else if (entry.type == VNODE_DEVICE)
			type = NATIVE_DT_CHR;
		else {
			(void)fd_set_offset(table, descriptor, before);
			if (written == 0)
				syscall_error(frame, SYSCALL_EIO);
			break;
		}
		record_length = (offsetof(struct native_dirent_record, name) +
		    name_length + 1U + sizeof(uint64_t) - 1U) &
		    ~(sizeof(uint64_t) - 1U);
		if (record_length > UINT16_MAX || record_length > capacity - written) {
			(void)fd_set_offset(table, descriptor, before);
			if (written == 0)
				syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		for (index = 0; index < record_length; index++)
			output[written + index] = 0;
		record = (struct native_dirent_record *)(void *)(output + written);
		record->inode = entry.inode;
		record->offset = (int64_t)after;
		record->record_length = (uint16_t)record_length;
		record->type = type;
		for (index = 0; index <= name_length; index++)
			record->name[index] = entry.name[index];
		written += record_length;
	}
	if (written != 0 && !vmm_copy_to_user(vmm_active_address_space(),
	    frame->rsi, output, written)) {
		/* The full range was validated above; retain position on any fault. */
		(void)fd_set_offset(table, descriptor, start_offset);
		syscall_error(frame, ERRNO_EFAULT);
		return frame;
	}
	if ((frame->rflags & 1U) == 0)
		frame->rax = written;
	return frame;
}

static struct interrupt_frame *
syscall_interrupt(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	uint64_t error = 0;
	struct fd_table *table;
	size_t amount;

	/* Generic syscall safe point: network progress is not tied to UDP calls. */
	(void)network_service_request(NETWORK_SERVICE_POLL_RX);
	(void)network_service_run(NETWORK_SERVICE_MAX_EVENTS, NULL);
	process_syscall_retry_begin();
	frame->rflags &= ~1ULL;
	switch (number) {
	case SYSCALL_GET_ABI_VERSION:
		frame->rax = SYSCALL_ABI_VERSION;
		break;
	case SYSCALL_POWER_CONTROL:
		{
			struct os_credentials credentials;
			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
		}
		if (frame->rdi == POWER_CONTROL_POWEROFF) {
			/* QEMU/Bochs power-management ports. */
			__asm__ volatile ("outw %0, %1" : : "a" ((uint16_t)0x2000),
			    "Nd" ((uint16_t)0x0604));
			__asm__ volatile ("outw %0, %1" : : "a" ((uint16_t)0x2000),
			    "Nd" ((uint16_t)0xb004));
		} else if (frame->rdi == POWER_CONTROL_REBOOT) {
			uint8_t reset = 0x02;
			__asm__ volatile ("outb %0, %1" : : "a" (reset),
			    "Nd" ((uint16_t)0x0cf9));
			for (volatile unsigned int delay = 0; delay < 100000; delay++)
				__asm__ volatile ("pause");
			reset = 0x06;
			__asm__ volatile ("outb %0, %1" : : "a" (reset),
			    "Nd" ((uint16_t)0x0cf9));
		} else {
			syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		/* If firmware did not act, stop this CPU instead of returning success. */
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	case SYSCALL_FRAMEBUFFER_INFO:
		{
			struct os_framebuffer_info info;
			struct address_space *space = vmm_active_address_space();

			if (!framebuffer_device_info(&info)) {
				syscall_error(frame, ERRNO_ENODEV);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rdi, sizeof(info)) ||
			    !vmm_copy_to_user(space, frame->rdi, &info, sizeof(info))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_FRAMEBUFFER_PRESENT:
		{
			int result = framebuffer_device_present(frame->rdi,
			    (size_t)frame->rsi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_FRAMEBUFFER_PRESENT_RECT:
		{
			struct os_framebuffer_rect rect;
			int result;

			if (!vmm_copy_from_user(vmm_active_address_space(), &rect,
			    frame->rdi, sizeof(rect))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = framebuffer_device_present_rect(&rect);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_KEYBOARD_POLL:
		{
			struct os_credentials credentials;
			struct terminal_key_event event;
			struct os_keyboard_event output;
			struct address_space *space = vmm_active_address_space();

			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rdi, sizeof(output))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			if (!ps2_keyboard_available()) {
				syscall_error(frame, ERRNO_ENODEV);
				break;
			}
			if (!ps2_keyboard_event_poll(&event)) {
				frame->rax = 0;
				break;
			}
			output = (struct os_keyboard_event){
				.scancode = event.scancode,
				.extended = event.extended,
				.pressed = event.pressed,
				.reserved = 0
			};
			if (!vmm_copy_to_user(space, frame->rdi, &output,
			    sizeof(output))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 1;
		}
		break;
	case SYSCALL_MOUSE_POLL:
		{
			struct os_credentials credentials;
			struct os_mouse_event output;
			struct address_space *space = vmm_active_address_space();

			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(output))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!ps2_mouse_available()) {
				syscall_error(frame, ERRNO_ENODEV);
				break;
			}
			if (!ps2_mouse_event_poll(&output)) {
				frame->rax = 0;
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, &output,
			    sizeof(output))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 1;
		}
		break;
	case SYSCALL_UNIX_SOCKET:
		{
			int result = unix_socket_create(current_fd_table(),
			    (int)frame->rdi, (int)frame->rsi, (int)frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UNIX_BIND:
	case SYSCALL_UNIX_CONNECT:
		{
			struct unix_socket_address address;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rdx < sizeof(address.family) + 1 ||
			    frame->rdx > sizeof(address)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &address,
			    frame->rsi, (size_t)frame->rdx)) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = number == SYSCALL_UNIX_BIND ?
			    unix_socket_bind(object, &address, (size_t)frame->rdx) :
			    unix_socket_connect(object, &address, (size_t)frame->rdx);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_LISTEN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result = unix_socket_listen(object, (int)frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_ACCEPT:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			result = unix_socket_accept(current_fd_table(), object);
			if (result == -EAGAIN && fd_object_nonblocking(object)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (result == -EAGAIN)
				return process_block_on_channel(frame,
				    pty_is_object(object) ? fd_readiness_wait_channel() :
				    unix_socket_wait_channel(object, true));
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else if (frame->rsi != 0) {
				uint32_t capacity;
				uint32_t actual = sizeof(uint16_t);
				uint16_t family = UNIX_SOCKET_AF_UNIX;
				if (frame->rdx == 0 || !vmm_copy_from_user(
				    vmm_active_address_space(), &capacity, frame->rdx,
				    sizeof(capacity)) || capacity < sizeof(family) ||
				    !vmm_copy_to_user(vmm_active_address_space(), frame->rsi,
				    &family, sizeof(family)) || !vmm_copy_to_user(
				    vmm_active_address_space(), frame->rdx, &actual,
				    sizeof(actual))) {
					(void)fd_close(current_fd_table(), (uint64_t)result);
					syscall_error(frame, EFAULT);
				} else {
					frame->rax = (uint64_t)result;
				}
			} else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_EXIT:
		return process_exit(frame, frame->rdi);
	case SYSCALL_WRITE:
		if (frame->rdx > SYSCALL_MAX_IO) {
			syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		{
			char buffer[SYSCALL_MAX_IO];
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object;
			int result;

			if (frame->rdx != 0 &&
			    !vmm_copy_from_user(space, buffer, frame->rsi,
			    (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			table = current_fd_table();
			object = fd_object_for_descriptor(table, frame->rdi);
			if (pipe_is_write_endpoint(object)) {
				result = pipe_write_try(object, buffer,
				    (size_t)frame->rdx, &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    fd_object_nonblocking(object)) {
					syscall_error(frame, ERRNO_EAGAIN);
					break;
				}
				if (result == -(int)ERRNO_EAGAIN)
					return process_block_on_channel(frame,
					    pipe_wait_channel(object, PIPE_WRITE_WAIT));
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				frame->rax = amount;
				break;
			}
			result = fd_write(table, frame->rdi, buffer,
			    (size_t)frame->rdx, &amount);
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)) &&
			    fd_object_nonblocking(object)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)))
				return process_block_on_channel(frame,
				    unix_socket_is_object(object) ?
				    unix_socket_wait_channel(object, false) :
				    fd_readiness_wait_channel());
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			frame->rax = amount;
		}
		break;
	case SYSCALL_GETPID:
		frame->rax = process_current_pid();
		break;
	case SYSCALL_GETPPID:
		frame->rax = process_current_ppid();
		break;
	case SYSCALL_GETCREDENTIALS:
		{
			struct os_credentials credentials;
			struct address_space *space = vmm_active_address_space();
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(credentials)) ||
			    !process_current_credentials(&credentials) ||
			    !vmm_copy_to_user(space, frame->rdi, &credentials,
			    sizeof(credentials)))
				syscall_error(frame, ERRNO_EFAULT);
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_SETUID:
	case SYSCALL_SETEUID:
	case SYSCALL_SETGID:
	case SYSCALL_SETEGID:
		if (frame->rdi > UINT32_MAX) {
			syscall_error(frame, ERRNO_EINVAL);
		} else {
			int result = number == SYSCALL_SETUID ?
			    process_set_uid((uint32_t)frame->rdi, false) :
			    number == SYSCALL_SETEUID ?
			    process_set_uid((uint32_t)frame->rdi, true) :
			    number == SYSCALL_SETGID ?
			    process_set_gid((uint32_t)frame->rdi, false) :
			    process_set_gid((uint32_t)frame->rdi, true);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_SETRESUID:
	case SYSCALL_SETRESGID:
		{
			int result;
			if (frame->rdi > UINT32_MAX || frame->rsi > UINT32_MAX ||
			    frame->rdx > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = number == SYSCALL_SETRESUID ?
			    process_set_res_uid((uint32_t)frame->rdi,
			    (uint32_t)frame->rsi, (uint32_t)frame->rdx) :
			    process_set_res_gid((uint32_t)frame->rdi,
			    (uint32_t)frame->rsi, (uint32_t)frame->rdx);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UMASK:
		if (frame->rdi > UINT32_MAX)
			syscall_error(frame, ERRNO_EINVAL);
		else
			frame->rax = process_umask((uint32_t)frame->rdi);
		break;
	case SYSCALL_CHMOD:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			struct os_credentials credentials;
			int result;
			if (frame->rsi > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ERRNO_ESRCH;
			if (result == 0)
				result = vfs_chmod_as(canonical, (uint32_t)frame->rsi,
				    credentials.effective_uid, credentials.effective_gid);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_MKDIR:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			struct os_credentials credentials;
			struct vnode *created = NULL;
			int result;
			if (frame->rsi > 07777U) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ERRNO_ESRCH;
			if (result == 0)
				result = vfs_create_path_as(canonical, VNODE_DIRECTORY,
				    (uint32_t)frame->rsi & ~process_get_umask(),
				    credentials.effective_uid,
				    credentials.effective_gid, &created);
			if (created != NULL)
				vnode_put(created);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_ACCESS:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			struct os_credentials credentials;
			struct vnode *node = NULL;
			unsigned int requested;
			int result;
			if (frame->rsi > 7U) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ERRNO_ESRCH;
			if (result == 0)
				result = vfs_lookup_as(canonical, credentials.real_uid,
				    credentials.real_gid, &node);
			requested = ((frame->rsi & 4U) ? VFS_ACCESS_READ : 0U) |
			    ((frame->rsi & 2U) ? VFS_ACCESS_WRITE : 0U) |
			    ((frame->rsi & 1U) ? VFS_ACCESS_EXEC : 0U);
			if (result == 0 && !vfs_access(node, credentials.real_uid,
			    credentials.real_gid, requested))
				result = -13;
			if (node != NULL)
				vnode_put(node);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_FCHMOD:
		{
			struct os_credentials credentials;
			struct fd_metadata metadata;
			struct fd_object *object;
			int result;
			if (frame->rsi > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			object = fd_object_for_descriptor(current_fd_table(),
			    frame->rdi);
			result = object == NULL ? -EBADF :
			    fd_object_stat(object, &metadata);
			if (result == 0 && !process_current_credentials(&credentials))
				result = -ERRNO_ESRCH;
			if (result == 0) {
				if (credentials.effective_uid != 0 &&
				    credentials.effective_uid != metadata.uid)
					result = -13;
				else if ((uint32_t)frame->rsi & 02000U &&
				    credentials.effective_uid != 0 &&
				    credentials.effective_gid != metadata.gid)
					result = -13;
				else
					result = fd_object_chmod(object,
					    (uint32_t)frame->rsi);
			}
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_KLDLOAD:
		{
			char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
			char name[KERNEL_MODULE_NAME_MAX + 1U];
			struct os_credentials credentials;
			struct vnode *node = NULL;
			struct fd_object *file = NULL;
			void *image = NULL;
			size_t image_size, offset = 0, name_length;
			int result;
			enum kernel_module_error module_error;
			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_resolve_path(path, canonical,
				    sizeof(canonical));
			if (result == 0)
				result = vfs_lookup_as(canonical, 0, 0, &node);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (node->type != VNODE_REGULAR || node->size == 0 ||
			    node->size > 8U * 1024U * 1024U || node->size > SIZE_MAX) {
				vnode_put(node);
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			image_size = (size_t)node->size;
			vnode_put(node);
			name_length = strlen(canonical);
			while (name_length != 0 && canonical[name_length - 1U] != '/')
				name_length--;
			{
				size_t begin = name_length;
				name_length = strlen(canonical) - begin;
				if (name_length > 3U &&
				    canonical[begin + name_length - 3U] == '.' &&
				    canonical[begin + name_length - 2U] == 'k' &&
				    canonical[begin + name_length - 1U] == 'o')
					name_length -= 3U;
				if (name_length == 0 || name_length > KERNEL_MODULE_NAME_MAX) {
					syscall_error(frame, ERRNO_EINVAL);
					break;
				}
				memcpy(name, canonical + begin, name_length);
				name[name_length] = '\0';
			}
			result = vfs_open_as(canonical, VFS_O_RDONLY, 0, 0, 0, &file);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			image = kheap_alloc(image_size);
			if (image == NULL) {
				fd_object_put(file);
				syscall_error(frame, ERRNO_ENOMEM);
				break;
			}
			while (offset < image_size) {
				size_t amount = 0;
				result = fd_object_read(file, (uint8_t *)image + offset,
				    image_size - offset, &amount);
				if (result != 0 || amount == 0)
					break;
				offset += amount;
			}
			fd_object_put(file);
			if (result != 0 || offset != image_size) {
				(void)kheap_free(image);
				syscall_error(frame, result == 0 ? 5U :
				    errno_from_kernel(result));
				break;
			}
			module_error = kernel_module_load(name, image, image_size);
			(void)kheap_free(image);
			if (module_error != KERNEL_MODULE_OK)
				syscall_error(frame, errno_from_module(module_error));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_KLDUNLOAD:
		{
			char name[KERNEL_MODULE_NAME_MAX + 1U];
			struct os_credentials credentials;
			enum kernel_module_error module_error;
			int result;
			if (!process_current_credentials(&credentials) ||
			    credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			result = copy_user_string(frame->rdi, name, sizeof(name));
			if (result != 0) {
				syscall_error(frame, (uint64_t)-result);
				break;
			}
			module_error = kernel_module_unload(name);
			if (module_error != KERNEL_MODULE_OK)
				syscall_error(frame, errno_from_module(module_error));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_CHOWN:
	case SYSCALL_FCHOWN:
		{
			struct os_credentials credentials;
			int result;
			if (frame->rsi > UINT32_MAX || frame->rdx > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ERRNO_ESRCH);
				break;
			}
			if (credentials.effective_uid != 0) {
				syscall_error(frame, 1U);
				break;
			}
			if (number == SYSCALL_FCHOWN) {
				struct fd_object *object = fd_object_for_descriptor(
				    current_fd_table(), frame->rdi);
				result = fd_object_chown(object, (uint32_t)frame->rsi,
				    (uint32_t)frame->rdx);
			} else {
				char path[VFS_PATH_MAX], canonical[VFS_PATH_MAX];
				result = copy_user_string(frame->rdi, path,
				    sizeof(path));
				if (result == 0)
					result = process_resolve_path(path, canonical,
					    sizeof(canonical));
				if (result == 0)
					result = vfs_chown_as(canonical,
					    (uint32_t)frame->rsi, (uint32_t)frame->rdx,
					    credentials.effective_uid,
					    credentials.effective_gid);
			}
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_CHDIR:
		{
			char path[VFS_PATH_MAX];
			int result = copy_user_string(frame->rdi, path, sizeof(path));
			if (result == 0)
				result = process_chdir(path);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_GETCWD:
		{
			char path[VFS_PATH_MAX];
			int length;
			if (frame->rsi == 0) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			length = process_getcwd(path, frame->rsi > sizeof(path) ?
			    sizeof(path) : (size_t)frame->rsi);
			if (length < 0) {
				syscall_error(frame, (uint64_t)-length);
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdi,
			    path, (size_t)length)) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_PROCESS_SNAPSHOT:
		{
			struct process_snapshot_entry entries[PROCESS_SNAPSHOT_MAX];
			uint64_t capacity = frame->rsi;
			int count;
			if (capacity == 0 || capacity > PROCESS_SNAPSHOT_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdi, (size_t)capacity * sizeof(entries[0]))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			count = process_snapshot(entries, (size_t)capacity);
			if (count < 0) {
				syscall_error(frame, (uint64_t)(-count));
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdi,
			    entries, (size_t)count * sizeof(entries[0]))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = (uint64_t)count;
		}
		break;
	case SYSCALL_FORK:
		frame = process_fork(frame, &error);
		break;
	case SYSCALL_EXEC:
		frame = process_exec(frame, &error);
		break;
	case SYSCALL_EXEC_PATH:
		{
			char path[VFS_PATH_MAX];
			int result = copy_user_string(frame->rdi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			frame = process_exec_path(frame, path, &error);
		}
		break;
	case SYSCALL_EXEC_PATH_ARGV:
		{
			char path[VFS_PATH_MAX];
			char argument_storage[2048];
			const char *arguments[16];
			uint64_t argc = frame->rsi;
			uint64_t vector = frame->rdx;
			size_t used = 0;
			size_t index;
			int result = copy_user_string(frame->rdi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			if (argc == 0 || argc > 16) {
				syscall_error(frame, ERRNO_E2BIG);
				break;
			}
			if (vector > UINT64_MAX - (argc + 1) * sizeof(uint64_t)) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			for (index = 0; index < argc; index++) {
				uint64_t address;
				size_t remaining = sizeof(argument_storage) - used;
				size_t length = 0;
				result = vmm_copy_from_user(vmm_active_address_space(),
				    &address, vector + index * sizeof(address), sizeof(address)) ?
				    0 : -ERRNO_EFAULT;
				if (result == 0 && remaining != 0)
					result = copy_user_string(address,
					    argument_storage + used, remaining);
				else if (result == 0)
					result = -ERRNO_E2BIG;
				if (result != 0) {
					syscall_error(frame, result == -(int)ERRNO_ENAMETOOLONG ?
					    ERRNO_E2BIG : (uint64_t)(-result));
					break;
				}
				arguments[index] = argument_storage + used;
				while (argument_storage[used + length] != '\0')
					length++;
				used += length + 1;
			}
			if (index != argc)
				break;
			{
				uint64_t terminator;
				if (!vmm_copy_from_user(vmm_active_address_space(),
				    &terminator, vector + argc * sizeof(terminator),
				    sizeof(terminator)) || terminator != 0) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
			}
			frame = process_exec_path_argv(frame, path, (size_t)argc,
			    arguments, &error);
		}
		break;
	case SYSCALL_WAITPID:
		frame = process_waitpid(frame, &error);
		break;
	case SYSCALL_SLEEP_TICKS:
		frame = process_sleep_ticks(frame, frame->rdi, &error);
		break;
	case SYSCALL_NANOSLEEP:
		{
			struct native_timespec request;
			struct address_space *space = vmm_active_address_space();
			uint64_t ticks;

			if (!vmm_copy_from_user(space, &request, frame->rdi,
			    sizeof(request))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			if (request.tv_sec < 0 || request.tv_nsec < 0 ||
			    request.tv_nsec >= 1000000000LL) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			if (frame->rsi != 0 && !vmm_validate_user_write(space,
			    frame->rsi, sizeof(request))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			if ((uint64_t)request.tv_sec >
			    (UINT64_MAX - 1U) / MONOTONIC_TICK_HZ) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			ticks = (uint64_t)request.tv_sec * MONOTONIC_TICK_HZ;
			if (request.tv_nsec != 0) {
				uint64_t fractional_ticks =
				    ((uint64_t)request.tv_nsec + MONOTONIC_TICK_NS - 1U) /
				    MONOTONIC_TICK_NS;
				if (ticks > UINT64_MAX - fractional_ticks - 1U) {
					syscall_error(frame, ERRNO_EINVAL);
					break;
				}
				ticks += fractional_ticks;
			}
			if (ticks != 0) {
				if (ticks == UINT64_MAX) {
					syscall_error(frame, ERRNO_EINVAL);
					break;
				}
				/* One extra tick accounts for unknown phase within the
				 * current tick; the wait therefore cannot finish early. */
				ticks++;
			}
			frame = process_nanosleep(frame, ticks, request.tv_sec,
			    request.tv_nsec, frame->rsi, &error);
		}
		break;
	case SYSCALL_CLOCK_GETTIME:
		{
			struct native_timespec value;
			struct address_space *space = vmm_active_address_space();
			uint64_t ticks;

			if (frame->rdi != CLOCK_MONOTONIC_ID) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rsi,
			    sizeof(value))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			ticks = timer_ticks();
			value.tv_sec = (int64_t)(ticks / MONOTONIC_TICK_HZ);
			value.tv_nsec = (int64_t)(ticks % MONOTONIC_TICK_HZ) *
			    (1000000000U / MONOTONIC_TICK_HZ);
			if (!vmm_copy_to_user(space, frame->rsi, &value,
			    sizeof(value))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_PTY_PAIR:
	case SYSCALL_PIPE:
		{
			uint32_t descriptors[2];
			int read_descriptor;
			int write_descriptor;
			int result;

			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdi, sizeof(descriptors))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			table = current_fd_table();
			result = number == SYSCALL_PTY_PAIR ?
			    pty_create(table, &read_descriptor, &write_descriptor) :
			    pipe_create(table, &read_descriptor, &write_descriptor);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			descriptors[0] = (uint32_t)read_descriptor;
			descriptors[1] = (uint32_t)write_descriptor;
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdi,
			    descriptors, sizeof(descriptors))) {
				(void)fd_close(table, (uint64_t)read_descriptor);
				(void)fd_close(table, (uint64_t)write_descriptor);
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_KILL:
		error = frame->rsi > UINT32_MAX ? ERRNO_EINVAL :
		    process_kill(frame->rdi, (unsigned int)frame->rsi);
		if (error != 0)
			syscall_error(frame, error);
		else
			frame->rax = 0;
		break;
	case SYSCALL_SIGACTION:
		if (frame->rdi > UINT32_MAX)
			syscall_error(frame, ERRNO_EINVAL);
		else
			frame = process_sigaction(frame, (unsigned int)frame->rdi,
			    frame->rsi, frame->rdx, &error);
		break;
	case SYSCALL_SIGPROCMASK:
		{
			uint64_t requested, previous;
			uint64_t *requested_pointer = NULL;
			uint64_t *previous_pointer = NULL;
			int result;
			struct address_space *space = vmm_active_address_space();

			if (frame->rsi != 0) {
				if (!vmm_copy_from_user(space, &requested, frame->rsi,
				    sizeof(requested))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				requested_pointer = &requested;
			}
			if (frame->rdx != 0) {
				if (!vmm_validate_user_write(space, frame->rdx,
				    sizeof(previous))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				previous_pointer = &previous;
			}
			if (frame->rdi > INT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = process_signal_mask((int)frame->rdi,
			    requested_pointer, previous_pointer);
			if (result < 0) {
				syscall_error(frame, (uint64_t)-result);
				break;
			}
			if (previous_pointer != NULL && !vmm_copy_to_user(space,
			    frame->rdx, &previous, sizeof(previous))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_SIGRETURN:
		frame = process_sigreturn(frame, &error);
		break;
	case SYSCALL_SHM_CREATE:
		{
			uint64_t base;
			int result = vmm_shm_create(vmm_active_address_space(),
			    frame->rdi, &base);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = base;
		}
		break;
	case SYSCALL_SHM_DETACH:
		{
			int result = vmm_shm_detach(vmm_active_address_space(),
			    frame->rdi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_SET_FS_BASE:
		error = process_set_fs_base(frame->rdi);
		if (error != 0)
			syscall_error(frame, error);
		else
			frame->rax = 0;
		break;
	case SYSCALL_MMAP:
		{
			uint64_t base;
			int result;

			if (frame->rsi > UINT32_MAX)
				result = -EINVAL;
			else
				result = vmm_private_map_protected(
				    vmm_active_address_space(), frame->rdi,
				    (unsigned int)frame->rsi, &base);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = base;
		}
		break;
	case SYSCALL_MUNMAP:
		{
			int result = vmm_private_unmap_complete(
			    vmm_active_address_space(), frame->rdi, frame->rsi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_MPROTECT:
		{
			int result;

			if (frame->rdx > UINT32_MAX) {
				result = -EINVAL;
			} else {
				result = vmm_private_protect(
				    vmm_active_address_space(), frame->rdi,
				    frame->rsi, (unsigned int)frame->rdx);
			}
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_WRITEV:
		return syscall_writev(frame);
	case SYSCALL_READV:
		return syscall_readv(frame);
	case SYSCALL_IOCTL:
		{
			struct syscall_winsize window = { 0 };
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);

			if (object == NULL) {
				syscall_error(frame, 9);
				break;
			}
			if (frame->rsi == OS_FIONBIO) {
				int enabled;
				if (!vmm_copy_from_user(vmm_active_address_space(),
				    &enabled, frame->rdx, sizeof(enabled))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				if (enabled)
					object->status_flags |= FD_STATUS_NONBLOCK;
				else
					object->status_flags &= ~FD_STATUS_NONBLOCK;
				fd_readiness_wake();
				frame->rax = 0;
				break;
			}
			if (frame->rsi == OS_FIONREAD) {
				size_t available;
				int result;
				if (!(unix_socket_is_object(object) || pty_is_object(object))) {
					syscall_error(frame, ERRNO_ENOTTY);
					break;
				}
				if (!vmm_validate_user_write(vmm_active_address_space(),
				    frame->rdx, sizeof(int))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				result = pty_is_object(object) ? pty_bytes_available(object, &available) :
				    unix_socket_bytes_available(object, &available);
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				int value = (int)available;
				if (!vmm_copy_to_user(vmm_active_address_space(),
				    frame->rdx, &value, sizeof(value))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				frame->rax = 0;
				break;
			}
			if (frame->rsi == TIOCSFB_REQUEST) {
				int enabled;

				if (!fd_object_is_console(object)) {
					syscall_error(frame, ERRNO_ENOTTY);
					break;
				}
				if (!vmm_copy_from_user(vmm_active_address_space(), &enabled,
				    frame->rdx, sizeof(enabled))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				if ((enabled != 0 && enabled != 1) ||
				    !tty_console_framebuffer_set(enabled != 0,
				    process_current_pid())) {
					syscall_error(frame, enabled != 0 && enabled != 1 ?
					    ERRNO_EINVAL : 1U);
					break;
				}
				frame->rax = 0;
				break;
			}
			if (pty_is_object(object) &&
			    (frame->rsi == TIOCGWINSZ_REQUEST || frame->rsi == 0x5414U)) {
				uint16_t dimensions[4];
				bool set = frame->rsi == 0x5414U;
				struct address_space *space = vmm_active_address_space();
				if ((set && !vmm_copy_from_user(space, dimensions,
				    frame->rdx, sizeof(dimensions))) ||
				    (!set && !vmm_validate_user_write(space, frame->rdx,
				    sizeof(dimensions)))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				(void)pty_window(object, dimensions, set);
				if (!set && !vmm_copy_to_user(space, frame->rdx,
				    dimensions, sizeof(dimensions)))
					syscall_error(frame, ERRNO_EFAULT);
				else
					frame->rax = 0;
				break;
			}
			if ((pty_is_object(object) || fd_object_is_terminal(object)) &&
			    (frame->rsi == TCGETS_REQUEST || frame->rsi == TCSETS_REQUEST)) {
				struct syscall_termios {
					uint32_t input_flags, output_flags, control_flags, local_flags;
					uint8_t control_chars[32];
					uint32_t input_speed, output_speed;
				} attributes;
				bool set = frame->rsi == TCSETS_REQUEST;
				struct address_space *space = vmm_active_address_space();
				int result;

				if ((set && !vmm_copy_from_user(space, &attributes,
				    frame->rdx, sizeof(attributes))) ||
				    (!set && !vmm_validate_user_write(space, frame->rdx,
				    sizeof(attributes)))) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				if (pty_is_object(object))
					result = pty_termios(object, &attributes, set);
				else if (set)
					result = tty_set_raw_mode((attributes.local_flags & 2U) == 0) ?
					    0 : -ERRNO_ENOTTY;
				else {
					/* The native console exposes canonical/echo or raw input;
					 * PTYs retain their more complete termios implementation. */
					attributes = (struct syscall_termios){
						.input_flags = tty_raw_mode() ? 0 : 0x100U,
						.output_flags = 5U,
						.control_flags = 0xb0U,
						.local_flags = tty_raw_mode() ? 0 : 0xbU,
						.control_chars = {3, 28, 127, 21, 4, 0, 1}
					};
					result = 0;
				}
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				if (!set && !vmm_copy_to_user(space, frame->rdx,
				    &attributes, sizeof(attributes)))
					syscall_error(frame, ERRNO_EFAULT);
				else
					frame->rax = 0;
				break;
			}
			if (!fd_object_is_terminal(object)) {
				syscall_error(frame, ERRNO_ENOTTY);
				break;
			}
			if (frame->rsi == TIOCSRAW_REQUEST ||
			    frame->rsi == TIOCCANON_REQUEST) {
				if (pty_is_object(object)) {
					int result = pty_set_raw(object, frame->rsi == TIOCSRAW_REQUEST);
					if (result != 0) syscall_error(frame, errno_from_kernel(result));
					else frame->rax = 0;
					break;
				}
				if (!tty_set_raw_mode(frame->rsi == TIOCSRAW_REQUEST))
					syscall_error(frame, ERRNO_ENOTTY);
				else
					frame->rax = 0;
				break;
			}
			if (frame->rsi != TIOCGWINSZ_REQUEST) {
				syscall_error(frame, ERRNO_ENOTTY);
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdx, sizeof(window))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			if (!tty_dimensions(&window.rows, &window.columns) ||
			    !vmm_copy_to_user(vmm_active_address_space(), frame->rdx,
			    &window, sizeof(window))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_UDP_SOCKET:
		{
			int result = udp_socket_create_fd(current_fd_table(),
			    frame->rdi, frame->rsi, frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_BIND:
		{
			struct udp_socket_endpoint endpoint;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rdx != sizeof(endpoint)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->rsi, sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_bind(object, &endpoint);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UDP_CONNECT:
		{
			struct udp_socket_endpoint endpoint;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx != sizeof(endpoint) ||
			    !vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->rsi, sizeof(endpoint))) {
				syscall_error(frame, frame->rdx != sizeof(endpoint) ?
				    EINVAL : EFAULT);
				break;
			}
			result = udp_socket_connect(object, &endpoint);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UDP_SEND:
		{
			uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx > sizeof(payload) ||
			    (frame->rdx != 0 && !vmm_copy_from_user(
			    vmm_active_address_space(), payload, frame->rsi,
			    (size_t)frame->rdx))) {
				syscall_error(frame, frame->rdx > sizeof(payload) ?
				    EMSGSIZE : EFAULT);
				break;
			}
			result = udp_socket_send(object, payload, (size_t)frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_SENDTO:
		{
			struct udp_socket_endpoint endpoint;
			uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx > sizeof(payload) || frame->r10 == 0) {
				syscall_error(frame, frame->rdx > sizeof(payload) ?
				    EMSGSIZE : EFAULT);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->r10, sizeof(endpoint)) ||
			    (frame->rdx != 0 && !vmm_copy_from_user(
			    vmm_active_address_space(), payload, frame->rsi,
			    (size_t)frame->rdx))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_sendto(object, &endpoint, payload,
			    (size_t)frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_UDP_RECVFROM:
		{
			struct udp_socket_datagram datagram;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			uint64_t start;
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx > UDP_SOCKET_PAYLOAD_LIMIT || frame->r10 == 0) {
				syscall_error(frame, frame->rdx > UDP_SOCKET_PAYLOAD_LIMIT ?
				    EMSGSIZE : EFAULT);
				break;
			}
			if ((frame->rdx != 0 && !vmm_validate_user_write(
			    vmm_active_address_space(), frame->rsi,
			    (size_t)frame->rdx)) || !vmm_validate_user_write(
			    vmm_active_address_space(), frame->r10,
			    sizeof(struct udp_socket_endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			start = timer_ticks();
			for (;;) {
				result = udp_socket_peek(object, &datagram);
				if (result == 0)
					break;
				if (result != -EAGAIN) {
					syscall_error(frame, errno_from_kernel(result));
					return frame;
				}
				result = udp_socket_poll();
				if (result < 0) {
					syscall_error(frame, errno_from_kernel(result));
					return frame;
				}
				if (timer_ticks() - start >= UDP_SOCKET_POLL_TIMEOUT_TICKS) {
					syscall_error(frame, ETIMEDOUT);
					return frame;
				}
				timer_wait_for_interrupt();
			}
			if (datagram.length > frame->rdx) {
				syscall_error(frame, EMSGSIZE);
				break;
			}
			if ((datagram.length != 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), frame->rsi, datagram.payload,
			    datagram.length)) || !vmm_copy_to_user(
			    vmm_active_address_space(), frame->r10, &datagram.source,
			    sizeof(datagram.source))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = udp_socket_consume(object);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = datagram.length;
		}
		break;
	case SYSCALL_TCP_SOCKET:
		{
			int result = tcp_socket_create_fd(current_fd_table(),
			    frame->rdi, frame->rsi, frame->rdx);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_TCP_BIND:
	case SYSCALL_TCP_CONNECT:
		{
			struct tcp_socket_endpoint endpoint;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rdx != sizeof(endpoint) ||
			    !vmm_copy_from_user(vmm_active_address_space(), &endpoint,
			    frame->rsi, sizeof(endpoint))) {
				syscall_error(frame, frame->rdx != sizeof(endpoint) ?
				    EINVAL : EFAULT);
				break;
			}
			result = number == SYSCALL_TCP_BIND ?
			    tcp_socket_bind(object, &endpoint) :
			    tcp_socket_connect(object, &endpoint);
			if (result == -EINPROGRESS && !fd_object_nonblocking(object))
				return process_block_on_channel(frame,
				    fd_readiness_wait_channel());
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_LISTEN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			result = tcp_socket_listen(object, frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_ACCEPT:
		{
			struct tcp_socket_endpoint peer;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->rsi != 0 && !vmm_validate_user_write(space,
			    frame->rsi, sizeof(peer))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = tcp_socket_accept(current_fd_table(), object,
			    frame->rsi == 0 ? NULL : &peer);
			if (result == -EAGAIN && !fd_object_nonblocking(object))
				return process_block_on_channel(frame,
				    fd_readiness_wait_channel());
			if (result < 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (frame->rsi != 0 && !vmm_copy_to_user(space, frame->rsi,
			    &peer, sizeof(peer))) {
				(void)fd_close(current_fd_table(), (uint64_t)result);
				syscall_error(frame, EFAULT);
			} else {
				frame->rax = (uint64_t)result;
			}
		}
		break;
	case SYSCALL_TCP_SHUTDOWN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			result = tcp_socket_shutdown(object, frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_GETSOCKOPT:
		{
			int value;
			size_t actual;
			uint32_t length;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
				current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->r8 == 0 || !vmm_copy_from_user(space, &length,
			    frame->r8, sizeof(length)) || length < sizeof(value)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_validate_user_write(space, frame->r10,
			    sizeof(value)) || !vmm_validate_user_write(space, frame->r8,
			    sizeof(length))) {
				syscall_error(frame, EFAULT);
				break;
			}
			actual = length;
			result = tcp_socket_get_option(object, frame->rsi, frame->rdx,
			    &value, &actual);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			length = (uint32_t)actual;
			if (!vmm_validate_user_write(space, frame->r10, actual) ||
			    !vmm_validate_user_write(space, frame->r8, sizeof(length)) ||
			    !vmm_copy_to_user(space, frame->r10, &value, actual) ||
			    !vmm_copy_to_user(space, frame->r8, &length, sizeof(length))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_SETSOCKOPT:
		{
			int value;
			struct fd_object *object = fd_object_for_descriptor(
				current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (frame->r8 != sizeof(value)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (!vmm_copy_from_user(vmm_active_address_space(), &value,
			    frame->r10, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = tcp_socket_set_option(object, frame->rsi,
			    frame->rdx, &value, sizeof(value));
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_TCP_GETSOCKNAME:
	case SYSCALL_TCP_GETPEERNAME:
		{
			struct tcp_socket_endpoint endpoint;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
				current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!tcp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rsi,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = tcp_socket_get_name(object,
			    number == SYSCALL_TCP_GETPEERNAME, &endpoint);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(space, frame->rsi, &endpoint,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_UDP_GETSOCKNAME:
	case SYSCALL_UDP_GETPEERNAME:
		{
			struct udp_socket_endpoint endpoint;
			struct address_space *space = vmm_active_address_space();
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (!udp_socket_is_object(object)) {
				syscall_error(frame, ENOTSOCK);
				break;
			}
			if (!vmm_validate_user_write(space, frame->rsi,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			result = number == SYSCALL_UDP_GETSOCKNAME ?
			    udp_socket_local_endpoint(object, &endpoint) :
			    udp_socket_peer_endpoint(object, &endpoint);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(space, frame->rsi, &endpoint,
			    sizeof(endpoint))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_NET_GET_DNS_SERVER:
		{
			uint8_t address[4];
			struct address_space *space = vmm_active_address_space();
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(address))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!cyclone_net_dns_server(address)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, address,
			    sizeof(address))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_NET_GET_IPV4_CONFIG:
		{
			uint8_t config[12];
			struct address_space *space = vmm_active_address_space();
			if (!vmm_validate_user_write(space, frame->rdi,
			    sizeof(config))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!cyclone_net_ipv4_config(config, config + 4,
			    config + 8)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, config,
			    sizeof(config))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_ICMP_ECHO:
		{
			struct icmp_echo_request request;
			struct address_space *space = vmm_active_address_space();
			int result;
			if (frame->rdx != sizeof(request)) {
				syscall_error(frame, EINVAL);
				break;
			}
			if (frame->rdi == 0 ||
			    !vmm_validate_user_write(space, frame->rdi,
			    sizeof(request)) ||
			    !vmm_copy_from_user(space, &request, frame->rdi,
			    sizeof(request))) {
				syscall_error(frame, EFAULT);
				break;
			}
			for (size_t i = 0; i < sizeof(request.reply_source); i++)
				request.reply_source[i] = 0;
			request.elapsed_ticks = 0;
			result = network_icmp_echo(&request);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(space, frame->rdi, &request,
			    sizeof(request))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_OPEN:
		{
			char path[VFS_PATH_MAX];
			char canonical[VFS_PATH_MAX];
			struct fd_object *file;
			struct os_credentials credentials;
			int result = copy_user_string(frame->rdi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			result = process_resolve_path(path, canonical,
			    sizeof(canonical));
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (frame->rsi > UINT32_MAX || frame->rdx > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ERRNO_ESRCH);
				break;
			}
			result = vfs_open_as(canonical, (uint32_t)frame->rsi,
			    (uint32_t)frame->rdx & ~process_get_umask(),
			    credentials.effective_uid,
			    credentials.effective_gid, &file);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			table = current_fd_table();
			result = fd_install(table, file);
			if (result < 0) {
				fd_object_put(file);
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_CLOSE:
		{
			int result = fd_close(current_fd_table(), frame->rdi);

			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_FCNTL:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			switch (frame->rsi) {
			case NATIVE_F_GETFD: {
				uint32_t flags;
				int result = fd_get_descriptor_flags(current_fd_table(),
				    frame->rdi, &flags);
				if (result != 0)
					syscall_error(frame, errno_from_kernel(result));
				else
					frame->rax = flags;
				break;
			}
			case NATIVE_F_SETFD: {
				int result = fd_set_descriptor_flags(current_fd_table(),
				    frame->rdi, (uint32_t)frame->rdx);
				if (result != 0)
					syscall_error(frame, errno_from_kernel(result));
				else
					frame->rax = 0;
				break;
			}
			case NATIVE_F_GETFL:
				frame->rax = object->status_flags;
				break;
			case NATIVE_F_SETFL:
				object->status_flags = (object->status_flags &
				    NATIVE_O_ACCMODE) | ((uint32_t)frame->rdx &
				    NATIVE_O_NONBLOCK);
				fd_readiness_wake();
				frame->rax = 0;
				break;
			case NATIVE_F_SETOWN:
				syscall_error(frame, EOPNOTSUPP);
				break;
			default:
				syscall_error(frame, EOPNOTSUPP);
				break;
			}
		}
		break;
	case SYSCALL_DUP2:
		{
			int result;
			if (frame->rdi > UINT32_MAX || frame->rsi > UINT32_MAX) {
				syscall_error(frame, EBADF);
				break;
			}
			result = fd_dup2(current_fd_table(), frame->rdi, frame->rsi);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_DUP:
		{
			int result;
			if (frame->rdi > UINT32_MAX) {
				syscall_error(frame, EBADF);
				break;
			}
			result = fd_dup(current_fd_table(), frame->rdi);
			if (result < 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_LSEEK:
		{
			uint64_t position;
			int result;

			if (frame->rdx > 2U) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = fd_seek(current_fd_table(), frame->rdi,
			    (int64_t)frame->rsi, (int)frame->rdx, &position);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = position;
		}
		break;
	case SYSCALL_FSTAT:
		{
			struct fd_metadata metadata;
			struct native_kstat value;
			int result = fd_object_stat(fd_object_for_descriptor(
			    current_fd_table(), frame->rdi), &metadata);

			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			result = native_kstat_from_metadata(&value, &metadata);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rsi, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rsi,
			    &value, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_FSTATAT:
		{
			char path[VFS_PATH_MAX];
			char base_path[VFS_PATH_MAX];
			struct fd_object *directory;
			struct vnode *node = NULL;
			struct fd_metadata metadata;
			struct native_kstat value;
			struct os_credentials credentials;
			int64_t directory_fd = (int64_t)frame->rdi;
			uint32_t flags;
			int result;

			if (frame->r10 > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			flags = (uint32_t)frame->r10;
			if ((flags & NATIVE_AT_SYMLINK_NOFOLLOW) != 0) {
				syscall_error(frame, EOPNOTSUPP);
				break;
			}
			if (flags != 0) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = copy_user_string(frame->rsi, path, sizeof(path));
			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			if (path[0] != '/' && directory_fd != NATIVE_AT_FDCWD) {
				directory = fd_object_for_descriptor(current_fd_table(),
				    frame->rdi);
				if (directory == NULL) {
					syscall_error(frame, EBADF);
					break;
				}
				result = fd_object_get_path(directory, base_path,
				    sizeof(base_path));
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
			} else if (path[0] != '/') {
				result = process_getcwd(base_path, sizeof(base_path));
				if (result < 0) {
					syscall_error(frame, (uint64_t)-result);
					break;
				}
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ERRNO_ESRCH);
				break;
			}
			result = vfs_lookup_at_as(base_path, path,
			    credentials.effective_uid, credentials.effective_gid, &node);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			metadata = (struct fd_metadata){
				.size = node->size,
				.inode = node->inode,
				.mode = node->mode,
				.type = (uint32_t)node->type,
				.uid = node->uid,
				.gid = node->gid
			};
			result = native_kstat_from_metadata(&value, &metadata);
			vnode_put(node);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->rdx, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rdx,
			    &value, sizeof(value))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_READ:
		if (frame->rdx > SYSCALL_MAX_IO) {
			syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		{
			char buffer[SYSCALL_MAX_IO];
			uint64_t old_offset = fd_get_offset(current_fd_table(), frame->rdi);
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;

			if (pipe_is_read_endpoint(object)) {
				result = pipe_read_stage(object, buffer,
				    (size_t)frame->rdx, &amount);
				if (result == -(int)ERRNO_EAGAIN &&
				    fd_object_nonblocking(object)) {
					syscall_error(frame, ERRNO_EAGAIN);
					break;
				}
				if (result == -(int)ERRNO_EAGAIN)
					return process_block_on_channel(frame,
					    pipe_wait_channel(object, PIPE_READ_WAIT));
				if (result != 0) {
					syscall_error(frame, errno_from_kernel(result));
					break;
				}
				if (amount != 0 && !vmm_copy_to_user(
				    vmm_active_address_space(), frame->rsi, buffer,
				    amount)) {
					syscall_error(frame, ERRNO_EFAULT);
					break;
				}
				if (pipe_read_commit(object, amount) != 0) {
					syscall_error(frame, ERRNO_EINVAL);
					break;
				}
				frame->rax = amount;
				break;
			}
			result = fd_read(current_fd_table(), frame->rdi, buffer,
			    (size_t)frame->rdx, &amount);
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)) &&
			    fd_object_nonblocking(object)) {
				syscall_error(frame, ERRNO_EAGAIN);
				break;
			}
			if (result == -(int)ERRNO_EAGAIN &&
			    (unix_socket_is_object(object) || pty_is_object(object) ||
			    tcp_socket_is_object(object)))
				return process_block_on_channel(frame,
				    unix_socket_is_object(object) ?
				    unix_socket_wait_channel(object, true) :
				    fd_readiness_wait_channel());

			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (amount != 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), frame->rsi, buffer, amount)) {
				(void)fd_set_offset(current_fd_table(), frame->rdi, old_offset);
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = amount;
		}
		break;
	case SYSCALL_STAT:
		{
			char path[VFS_PATH_MAX];
			char canonical[VFS_PATH_MAX];
			struct vfs_stat status;
			struct os_credentials credentials;
			int result = copy_user_string(frame->rdi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			result = process_resolve_path(path, canonical,
			    sizeof(canonical));
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!process_current_credentials(&credentials)) {
				syscall_error(frame, ERRNO_ESRCH);
				break;
			}
			result = vfs_stat_path_as(canonical,
			    credentials.effective_uid, credentials.effective_gid,
			    &status);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (!vmm_copy_to_user(vmm_active_address_space(), frame->rsi,
			    &status, sizeof(status))) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_GETDENTS:
		return syscall_getdents(frame);
	case SYSCALL_READDIR:
		{
			struct vfs_dirent entry;
			uint64_t old_offset = fd_get_offset(current_fd_table(), frame->rdi);
			int result = fd_readdir(current_fd_table(), frame->rdi, &entry);

			if (result < 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			if (result > 0 && !vmm_copy_to_user(
			    vmm_active_address_space(), frame->rsi, &entry,
			    sizeof(entry))) {
				(void)fd_set_offset(current_fd_table(), frame->rdi, old_offset);
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			frame->rax = (uint64_t)result;
		}
		break;
	case SYSCALL_SELECT:
		return syscall_select(frame);
	case SYSCALL_UNIX_SHUTDOWN:
		{
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result = unix_socket_shutdown(object, (int)frame->rsi);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_GETSOCKOPT:
		{
			uint8_t value[16] = {0};
			uint32_t length;
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			size_t actual;
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rsi != UNIX_SOCKET_SOL_SOCKET || frame->r8 == 0 ||
			    !vmm_copy_from_user(vmm_active_address_space(), &length,
			    frame->r8, sizeof(length)) || length > sizeof(value)) {
				syscall_error(frame, EINVAL);
				break;
			}
			actual = length;
			result = unix_socket_get_option(object, (int)frame->rdx,
			    value, &actual);
			if (result != 0) {
				syscall_error(frame, errno_from_kernel(result));
				break;
			}
			length = (uint32_t)actual;
			if (!vmm_validate_user_write(vmm_active_address_space(),
			    frame->r10, actual) || !vmm_validate_user_write(
			    vmm_active_address_space(), frame->r8, sizeof(length)) ||
			    !vmm_copy_to_user(vmm_active_address_space(), frame->r10,
			    value, actual) || !vmm_copy_to_user(vmm_active_address_space(),
			    frame->r8, &length, sizeof(length))) {
				syscall_error(frame, EFAULT);
				break;
			}
			frame->rax = 0;
		}
		break;
	case SYSCALL_UNIX_SETSOCKOPT:
		{
			uint8_t value[16];
			struct fd_object *object = fd_object_for_descriptor(
			    current_fd_table(), frame->rdi);
			int result;
			if (object == NULL) {
				syscall_error(frame, EBADF);
				break;
			}
			if (frame->rsi != UNIX_SOCKET_SOL_SOCKET || frame->r8 == 0 ||
			    frame->r8 > sizeof(value) || !vmm_copy_from_user(
			    vmm_active_address_space(), value, frame->r10,
			    (size_t)frame->r8)) {
				syscall_error(frame, EINVAL);
				break;
			}
			result = unix_socket_set_option(object, (int)frame->rdx,
			    value, (size_t)frame->r8);
			if (result != 0)
				syscall_error(frame, errno_from_kernel(result));
			else
				frame->rax = 0;
		}
		break;
	default:
		syscall_error(frame, ERRNO_ENOSYS);
		break;
	}
	if (error != 0)
		syscall_error(frame, error);
	return frame;
}

void
syscall_init(struct fd_table *table)
{
	if (table == NULL || (!syscall_handler_registered &&
	    !interrupt_register_handler(SYSCALL_VECTOR, syscall_interrupt))) {
		klog("OpenSillicon: syscall initialization failed\n");
		kernel_halt();
	}
	syscall_handler_registered = true;
	descriptor_table = table;
}
