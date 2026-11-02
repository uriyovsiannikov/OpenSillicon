#ifndef ABI_SELECT_H
#define ABI_SELECT_H

#include <stdint.h>

#define OS_FD_SETSIZE 1024U
#define OS_FD_SET_WORDS (OS_FD_SETSIZE / 64U)

struct os_fd_set {
	uint64_t words[OS_FD_SET_WORDS];
};

struct os_timeval {
	int64_t seconds;
	int64_t microseconds;
};

#endif
