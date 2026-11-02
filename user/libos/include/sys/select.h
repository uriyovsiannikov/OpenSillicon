#ifndef LIBOS_SYS_SELECT_H
#define LIBOS_SYS_SELECT_H

#include <sys/time.h>
#include <sys/types.h>

#define FD_SETSIZE 1024
typedef long fd_mask;
typedef struct {
	union {
		fd_mask __fds_bits[FD_SETSIZE / (8 * sizeof(fd_mask))];
		fd_mask fds_bits[FD_SETSIZE / (8 * sizeof(fd_mask))];
	};
} fd_set;
#ifndef __FDS_BITS
#define __FDS_BITS(set) ((set)->__fds_bits)
#endif

#define FD_ZERO(set) do { \
	for (size_t __fd_i = 0; __fd_i < FD_SETSIZE / (8 * sizeof(unsigned long)); \
	    __fd_i++) (set)->__fds_bits[__fd_i] = 0; \
} while (0)
#define FD_SET(fd, set) do { \
	if ((fd) >= 0 && (fd) < FD_SETSIZE) \
		(set)->__fds_bits[(fd) / (8 * sizeof(fd_mask))] |= \
		    1UL << ((fd) % (8 * sizeof(fd_mask))); \
} while (0)
#define FD_CLR(fd, set) do { \
	if ((fd) >= 0 && (fd) < FD_SETSIZE) \
		(set)->__fds_bits[(fd) / (8 * sizeof(fd_mask))] &= \
		    ~(1UL << ((fd) % (8 * sizeof(fd_mask)))); \
} while (0)
#define FD_ISSET(fd, set) ((fd) >= 0 && (fd) < FD_SETSIZE && \
	(((set)->__fds_bits[(fd) / (8 * sizeof(fd_mask))] >> \
	((fd) % (8 * sizeof(fd_mask)))) & 1UL))

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	struct timeval *timeout);

#endif
