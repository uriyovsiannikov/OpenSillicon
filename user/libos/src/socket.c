#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "abi/syscall.h"
#include "internal.h"

int
getaddrinfo(const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **result)
{
	(void)node;
	(void)service;
	(void)hints;
	if (result != 0) *result = 0;
	errno = EOPNOTSUPP;
	return EAI_NONAME;
}

void freeaddrinfo(struct addrinfo *result) { (void)result; }
const char *gai_strerror(int error) { (void)error; return "name lookup unsupported"; }

ssize_t
send(int fd, const void *buffer, size_t length, int flags)
{
	if (flags != 0) {
		errno = EOPNOTSUPP;
		return -1;
	}
	return write(fd, buffer, length);
}

ssize_t
recv(int fd, void *buffer, size_t length, int flags)
{
	if (flags != 0) {
		errno = EOPNOTSUPP;
		return -1;
	}
	return read(fd, buffer, length);
}

int
socket(int domain, int type, int protocol)
{
	return (int)__libos_call(SYSCALL_UNIX_SOCKET, domain, type, protocol,
	    0, 0, 0);
}

int
bind(int fd, const struct sockaddr *address, socklen_t length)
{
	return (int)__libos_call(SYSCALL_UNIX_BIND, fd, (long)address, length,
	    0, 0, 0);
}

int
listen(int fd, int backlog)
{
	return (int)__libos_call(SYSCALL_UNIX_LISTEN, fd, backlog, 0, 0, 0, 0);
}

int
connect(int fd, const struct sockaddr *address, socklen_t length)
{
	return (int)__libos_call(SYSCALL_UNIX_CONNECT, fd, (long)address, length,
	    0, 0, 0);
}

int
accept(int fd, struct sockaddr *address, socklen_t *length)
{
	if ((address == 0) != (length == 0)) {
		errno = EINVAL;
		return -1;
	}
	return (int)__libos_call(SYSCALL_UNIX_ACCEPT, fd, (long)address,
	    (long)length, 0, 0, 0);
}

int
shutdown(int fd, int how)
{
	return (int)__libos_call(SYSCALL_UNIX_SHUTDOWN, fd, how, 0, 0, 0, 0);
}

int
getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
	if (length == 0) {
		errno = EINVAL;
		return -1;
	}
	return (int)__libos_call(SYSCALL_UNIX_GETSOCKOPT, fd, level, option,
	    (long)value, (long)length, 0);
}

int
setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
	return (int)__libos_call(SYSCALL_UNIX_SETSOCKOPT, fd, level, option,
	    (long)value, length, 0);
}
