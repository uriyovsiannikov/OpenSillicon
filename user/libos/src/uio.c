#include <sys/uio.h>

#include "abi/syscall.h"
#include "internal.h"

ssize_t
readv(int fd, const struct iovec *vectors, int count)
{
	return (ssize_t)__libos_call(SYSCALL_READV, fd, (long)vectors, count,
	    0, 0, 0);
}

ssize_t
writev(int fd, const struct iovec *vectors, int count)
{
	return (ssize_t)__libos_call(SYSCALL_WRITEV, fd, (long)vectors, count,
	    0, 0, 0);
}
