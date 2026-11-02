#include <sys/select.h>

#include "abi/select.h"
#include "abi/syscall.h"
#include "internal.h"

_Static_assert(sizeof(fd_set) == sizeof(struct os_fd_set),
	"libos fd_set ABI size mismatch");
_Static_assert(sizeof(struct timeval) == sizeof(struct os_timeval),
	"libos timeval ABI size mismatch");

int
select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	struct timeval *timeout)
{
	return (int)__libos_call(SYSCALL_SELECT, nfds, (long)readfds,
	    (long)writefds, (long)exceptfds, (long)timeout, 0);
}
