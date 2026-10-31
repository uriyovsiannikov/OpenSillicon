#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/user.h"
#include "kernel/fd.h"
#include "kernel/log.h"
#include "kernel/process.h"
#include "kernel/syscall.h"
#include "kernel/vmm.h"

#define SYSCALL_VECTOR 128U
#define SYSCALL_MAX_WRITE 4096U
#define ERRNO_EBADF 9U
#define ERRNO_EFAULT 14U
#define ERRNO_EINVAL 22U
#define ERRNO_ENOSYS 78U
#define ERRNO_ECHILD 10U
#define ERRNO_EAGAIN 11U
#define ERRNO_ENOMEM 12U

static struct fd_table *descriptor_table;

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

	frame->rflags &= ~1ULL;
	switch (number) {
	case SYSCALL_GET_ABI_VERSION:
		frame->rax = SYSCALL_ABI_VERSION;
		break;
	case SYSCALL_EXIT:
		return process_exit(frame, frame->rdi);
	case SYSCALL_WRITE:
		if (frame->rdx > SYSCALL_MAX_WRITE) {
			syscall_error(frame, ERRNO_EINVAL);
			break;
		}
		if (frame->rdx != 0) {
			char buffer[SYSCALL_MAX_WRITE];
			struct address_space *space = vmm_active_address_space();

			if (!vmm_copy_from_user(space, buffer, frame->rsi,
			    (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EFAULT);
				break;
			}
			table = process_current_fd_table();
			if (table == NULL)
				table = descriptor_table;
			if (!fd_write(table, frame->rdi, buffer,
			    (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EBADF);
				break;
			}
		}
		frame->rax = frame->rdx;
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
	case SYSCALL_WAITPID:
		frame = process_waitpid(frame, &error);
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
