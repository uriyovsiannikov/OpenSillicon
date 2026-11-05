#ifndef ABI_UNIX_SOCKET_H
#define ABI_UNIX_SOCKET_H

#include <stdint.h>

struct os_unix_ucred {
	int32_t pid;
	uint32_t uid;
	uint32_t gid;
};

#endif
