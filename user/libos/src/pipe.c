#include <unistd.h>

#include "abi/syscall.h"
#include "internal.h"

int
pipe(int descriptors[2])
{
	if (descriptors == 0)
		return __libos_call(SYSCALL_PIPE, 0, 0, 0, 0, 0, 0);
	return (int)__libos_call(SYSCALL_PIPE, (long)descriptors, 0, 0,
	    0, 0, 0);
}
