#include <errno.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "abi/syscall.h"
#include "internal.h"

int
openpty(int *master, int *slave, char *name,
	const struct termios *attributes, const struct winsize *size)
{
	int descriptors[2];
	int saved_errno;

	if (master == NULL || slave == NULL || master == slave) {
		errno = EINVAL;
		return -1;
	}
	if (name != NULL || attributes != NULL) {
		errno = EOPNOTSUPP;
		return -1;
	}
	if (__libos_call(SYSCALL_PTY_PAIR, (long)descriptors, 0, 0, 0, 0, 0) < 0)
		return -1;
	if (size != NULL && ioctl(descriptors[1], TIOCSWINSZ, size) < 0) {
		saved_errno = errno;
		close(descriptors[0]);
		close(descriptors[1]);
		errno = saved_errno;
		return -1;
	}
	*master = descriptors[0];
	*slave = descriptors[1];
	return 0;
}
