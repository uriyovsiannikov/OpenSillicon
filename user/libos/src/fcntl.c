#include <fcntl.h>
#include <stdarg.h>

#include "abi/syscall.h"
#include "internal.h"

int
fcntl(int fd, int command, ...)
{
	long argument = 0;
	if (command == F_SETFD || command == F_SETFL || command == F_SETOWN) {
		va_list args;
		va_start(args, command);
		argument = va_arg(args, int);
		va_end(args);
	}
	return (int)__libos_call(SYSCALL_FCNTL, fd, command, argument,
	    0, 0, 0);
}
