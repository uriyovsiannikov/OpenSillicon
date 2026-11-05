#include <sys/uio.h>
#include <errno.h>
#include <stddef.h>

#include "abi/syscall.h"
#include "internal.h"

/* Native vector I/O accepts at most 64 KiB. A short transfer lets the
 * caller retry the remaining data without breaking a stream protocol. */
static ssize_t
vector_io(long syscall, int fd, const struct iovec *vectors, int count)
{
	struct iovec limited[16];
	size_t remaining = 64U * 1024U;
	int used = 0;
	if (count < 0 || count > 16) { errno = EINVAL; return -1; }
	if (count != 0 && vectors == NULL) { errno = EFAULT; return -1; }
	while (used < count && remaining != 0) {
		limited[used] = vectors[used];
		if (limited[used].iov_len > remaining)
			limited[used].iov_len = remaining;
		remaining -= limited[used].iov_len;
		used++;
	}
	return (ssize_t)__libos_call(syscall, fd, (long)limited, used, 0, 0, 0);
}

ssize_t
readv(int fd, const struct iovec *vectors, int count)
{
	return vector_io(SYSCALL_READV, fd, vectors, count);
}

ssize_t
writev(int fd, const struct iovec *vectors, int count)
{
	return vector_io(SYSCALL_WRITEV, fd, vectors, count);
}
