#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/user.h"
#include "kernel/fd.h"
#include "kernel/errno.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/tty.h"
#include "kernel/vmm.h"
#include "kernel/vfs.h"
#include "kernel/network_service.h"
#include "kernel/udp_socket.h"
#include "kernel/icmp_echo.h"
#include "kernel/timer.h"

#define SYSCALL_VECTOR 128U
#define SYSCALL_MAX_IO 4096U
#define SYSCALL_MAX_IOV 16U
#define SYSCALL_MAX_IOV_BYTES (64U * 1024U)
#define SYSCALL_IOV_CHUNK 512U
#define TIOCGWINSZ_REQUEST 0x5413U
#define TIOCSRAW_REQUEST 0x4f530001U
#define TIOCCANON_REQUEST 0x4f530002U
#define ERRNO_EFAULT 14U
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
#define CLOCK_MONOTONIC_ID 1U
#define MONOTONIC_TICK_HZ 100U

struct native_timespec {
	int64_t tv_sec;
	int64_t tv_nsec;
};

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
					return process_block_on_channel(frame,
					    pipe_wait_channel(object, PIPE_WRITE_WAIT));
				}
			} else {
				result = fd_write(table, frame->rdi, buffer, chunk,
				    &amount);
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
					return process_block_on_channel(frame,
					    pipe_wait_channel(object, PIPE_READ_WAIT));
				}
			} else {
				result = fd_read(table, frame->rdi, buffer, chunk,
				    &amount);
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
			int result;

			if (frame->rdx != 0 &&
			    !vmm_copy_from_user(space, buffer, frame->rsi,
			    (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			table = current_fd_table();
			{
				struct fd_object *object = fd_object_for_descriptor(table,
				    frame->rdi);

				if (pipe_is_write_endpoint(object)) {
					result = pipe_write_try(object, buffer,
					    (size_t)frame->rdx, &amount);
					if (result == -(int)ERRNO_EAGAIN)
						return process_block_on_channel(frame,
						    pipe_wait_channel(object,
						    PIPE_WRITE_WAIT));
					if (result != 0) {
						syscall_error(frame,
						    errno_from_kernel(result));
						break;
					}
					frame->rax = amount;
					break;
				}
			}
			result = fd_write(table, frame->rdi, buffer,
			    (size_t)frame->rdx, &amount);
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
			result = pipe_create(table, &read_descriptor,
			    &write_descriptor);
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
			if (!fd_object_is_terminal(object)) {
				syscall_error(frame, ERRNO_ENOTTY);
				break;
			}
			if (frame->rsi == TIOCSRAW_REQUEST ||
			    frame->rsi == TIOCCANON_REQUEST) {
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
				__asm__ volatile ("pause" : : : "memory");
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
			struct fd_object *file;
			int result = copy_user_string(frame->rdi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			if (frame->rsi > UINT32_MAX || frame->rdx > UINT32_MAX) {
				syscall_error(frame, ERRNO_EINVAL);
				break;
			}
			result = vfs_open(path, (uint32_t)frame->rsi,
			    (uint32_t)frame->rdx, &file);
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
			struct vfs_stat status;
			int result = copy_user_string(frame->rdi, path, sizeof(path));

			if (result != 0) {
				syscall_error(frame, (uint64_t)(-result));
				break;
			}
			result = vfs_stat_path(path, &status);
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
	if (table == NULL || !interrupt_register_handler(SYSCALL_VECTOR,
	    syscall_interrupt)) {
		klog("OpenSillicon: syscall initialization failed\n");
		kernel_halt();
	}
	descriptor_table = table;
}
