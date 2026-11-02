#include <stdarg.h>
#include <sys/ioctl.h>

#include "abi/syscall.h"
#include "internal.h"

int
ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	void *argument;
	va_start(args, request);
	argument = va_arg(args, void *);
	va_end(args);
	return (int)__libos_call(SYSCALL_IOCTL, fd, (long)request,
	    (long)argument, 0, 0, 0);
}
