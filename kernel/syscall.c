#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/user.h"
#include "kernel/fd.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/vmm.h"
#include "kernel/vfs.h"

#define SYSCALL_VECTOR 128U
#define SYSCALL_MAX_IO 4096U
#define ERRNO_EFAULT 14U
#define ERRNO_EINVAL 22U
#define ERRNO_ENOSYS 78U
#define ERRNO_ECHILD 10U
#define ERRNO_EAGAIN 11U
#define ERRNO_ENOMEM 12U
#define ERRNO_ENAMETOOLONG 36U
#define ERRNO_EPIPE 32U
#define ERRNO_EMFILE 24U

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

static struct interrupt_frame *
syscall_interrupt(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;
	uint64_t error = 0;
	struct fd_table *table;
	size_t amount;

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
	case SYSCALL_WAITPID:
		frame = process_waitpid(frame, &error);
		break;
	case SYSCALL_SLEEP_TICKS:
		frame = process_sleep_ticks(frame, frame->rdi, &error);
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
