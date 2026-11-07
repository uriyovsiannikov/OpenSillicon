#ifndef LIBOS_TIME_H
#define LIBOS_TIME_H

#include <sys/types.h>

#ifndef LIBOS_STRUCT_TIMESPEC
#define LIBOS_STRUCT_TIMESPEC
struct timespec {
	long tv_sec;
	long tv_nsec;
};
#endif

typedef int clockid_t;
struct tm {
	int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
	int tm_wday, tm_yday, tm_isdst;
};

#define CLOCK_MONOTONIC 1
#define CLOCK_REALTIME 0

int clock_gettime(clockid_t, struct timespec *);
int nanosleep(const struct timespec *, struct timespec *);
time_t time(time_t *);
time_t mktime(struct tm *);
struct tm *gmtime(const time_t *);
struct tm *localtime(const time_t *);

#endif
