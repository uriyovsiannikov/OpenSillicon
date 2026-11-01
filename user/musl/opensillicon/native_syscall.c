/*
 * Native x86_64 syscall adapter for the bounded Stage 14J musl profile.
 * Linux syscall numbers are accepted only as musl-internal selectors here;
 * the kernel receives only OpenSillicon syscall numbers.
 */
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#define OS_SYSCALL_EXIT 1
#define OS_SYSCALL_WRITE 2
#define OS_SYSCALL_GETPID 3
#define OS_SYSCALL_OPEN 7
#define OS_SYSCALL_CLOSE 8
#define OS_SYSCALL_READ 9
#define OS_SYSCALL_SET_FS_BASE 20
#define OS_SYSCALL_MMAP 21
#define OS_SYSCALL_MUNMAP 22
#define OS_SYSCALL_MPROTECT 23
#define OS_SYSCALL_WRITEV 24
#define OS_SYSCALL_READV 25
#define OS_SYSCALL_IOCTL 26
#define OS_SYSCALL_CLOCK_GETTIME 34
#define OS_SYSCALL_NANOSLEEP 35

static long
unsupported(void)
{
	return -ENOSYS;
}

long
__opensillicon_syscall(long number, long a1, long a2, long a3, long a4,
	long a5, long a6)
{
	long native_number;
	long args[6] = { a1, a2, a3, a4, a5, a6 };
	long result = 0;
	unsigned char failed;
	register long r10 __asm__("r10") = 0;
	register long r8 __asm__("r8") = 0;
	register long r9 __asm__("r9") = 0;

	switch (number) {
	case SYS_exit:
	case SYS_exit_group:
		native_number = OS_SYSCALL_EXIT;
		break;
	case SYS_write:
		native_number = OS_SYSCALL_WRITE;
		break;
	case SYS_writev:
		native_number = OS_SYSCALL_WRITEV;
		break;
	case SYS_read:
		native_number = OS_SYSCALL_READ;
		break;
	case SYS_readv:
		native_number = OS_SYSCALL_READV;
		break;
	case SYS_ioctl:
		native_number = OS_SYSCALL_IOCTL;
		break;
	case SYS_clock_gettime:
		native_number = OS_SYSCALL_CLOCK_GETTIME;
		break;
	case SYS_nanosleep:
		native_number = OS_SYSCALL_NANOSLEEP;
		break;
	case SYS_close:
		native_number = OS_SYSCALL_CLOSE;
		break;
	case SYS_open:
		args[1] &= ~O_LARGEFILE;
		native_number = OS_SYSCALL_OPEN;
		break;
	case SYS_openat:
		if (a1 != AT_FDCWD)
			return unsupported();
		args[0] = a2;
		args[1] = a3;
		args[1] &= ~O_LARGEFILE;
		args[2] = a4;
		native_number = OS_SYSCALL_OPEN;
		break;
	case SYS_getpid:
	case SYS_set_tid_address:
		/* Single-thread profile: process id is also its only thread id. */
		native_number = OS_SYSCALL_GETPID;
		break;
	case SYS_arch_prctl:
		if (a1 != 0x1002) /* ARCH_SET_FS only; GET_FS is not exposed. */
			return unsupported();
		args[0] = a2;
		native_number = OS_SYSCALL_SET_FS_BASE;
		break;
	case SYS_mmap:
		if (a1 != 0 || a4 != (MAP_PRIVATE | MAP_ANONYMOUS) ||
		    a5 != -1 || a6 != 0)
			return -EINVAL;
		if ((a3 & PROT_EXEC) != 0)
			return -EOPNOTSUPP;
		if ((a3 & ~(PROT_READ | PROT_WRITE)) != 0)
			return -EINVAL;
		args[0] = a2;
		args[1] = a3;
		native_number = OS_SYSCALL_MMAP;
		break;
	case SYS_munmap:
		args[0] = a1;
		args[1] = a2;
		native_number = OS_SYSCALL_MUNMAP;
		break;
	case SYS_mprotect:
		args[0] = a1;
		args[1] = a2;
		args[2] = a3;
		native_number = OS_SYSCALL_MPROTECT;
		break;
	default:
		return unsupported();
	}

	result = native_number;
	r10 = args[3];
	r8 = args[4];
	r9 = args[5];
	__asm__ __volatile__("int $0x80\n\tsetc %0"
	    : "=m" (failed), "+a" (result)
	    : "D" (args[0]), "S" (args[1]), "d" (args[2]),
	      "r" (r10), "r" (r8), "r" (r9)
	    : "rcx", "r11", "memory", "cc");
	if (failed) {
		long error = result;
		/* OpenSillicon historically assigned ENOSYS=78; musl uses 38. */
		if (error == 78)
			error = ENOSYS;
		return -error;
	}
	return result;
}
