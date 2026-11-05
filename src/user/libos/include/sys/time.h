#ifndef LIBOS_SYS_TIME_H
#define LIBOS_SYS_TIME_H
#include <time.h>
typedef long suseconds_t;

struct timeval {
	long tv_sec;
	long tv_usec;
};
int gettimeofday(struct timeval *, void *);

#endif
