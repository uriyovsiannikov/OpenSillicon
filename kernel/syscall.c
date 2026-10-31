#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/user.h"
#include "kernel/fd.h"
#include "kernel/log.h"
#include "kernel/syscall.h"
#include "kernel/vmm.h"

#define SYSCALL_VECTOR 128U
#define SYSCALL_MAX_WRITE 4096U
#define ERRNO_EBADF 9U
#define ERRNO_EFAULT 14U
#define ERRNO_EINVAL 22U
#define ERRNO_ENOSYS 78U

static struct fd_table *descriptor_table;

static void
syscall_error(struct interrupt_frame *frame, uint64_t error)
{
	frame->rax = error;
	frame->rflags |= 1U;
}

static void
syscall_interrupt(struct interrupt_frame *frame)
{
	uint64_t number = frame->rax;

	frame->rflags &= ~1ULL;
	switch (number) {
	case SYSCALL_GET_ABI_VERSION:
		frame->rax = SYSCALL_ABI_VERSION;
		break;
	case SYSCALL_EXIT:
		user_return_to_kernel(frame->rdi);
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
			if (!fd_write(descriptor_table, frame->rdi, buffer,
			    (size_t)frame->rdx)) {
				syscall_error(frame, ERRNO_EBADF);
				break;
			}
		}
		frame->rax = frame->rdx;
		break;
	case SYSCALL_GETPID:
		frame->rax = 1;
		break;
	default:
		syscall_error(frame, ERRNO_ENOSYS);
		break;
	}
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
