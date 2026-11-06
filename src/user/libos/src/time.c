#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "abi/syscall.h"
#include "internal.h"

static int leap_year(int64_t year){return year%4==0&&(year%100!=0||year%400==0);}

struct tm *
gmtime(const time_t *value)
{
	static struct tm result;
	static const int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
	long seconds, days, day_seconds;
	int64_t year = 1970;
	int month, yday=0;
	if(value==NULL)return NULL;
	seconds=*value;
	days=seconds/86400;
	day_seconds=seconds%86400;
	if(day_seconds<0){day_seconds+=86400;days--;}
	if(days<0)return NULL;
	/* Skip complete Gregorian 400-year cycles (146097 days). */
	{
		uint64_t cycles = (uint64_t)days / 146097U;
		year += (int64_t)(cycles * 400U);
		days = (long)((uint64_t)days % 146097U);
	}
	while(days >= (leap_year(year)?366:365)){days-=leap_year(year)?366:365;year++;}
	if (year - 1900 > INT_MAX)
		return NULL;
	yday=(int)days;
	for(month=0;month<11;month++){int span=month_days[month]+(month==1&&leap_year(year));if(days<span)break;days-=span;}
	result.tm_sec=(int)(day_seconds%60);
	result.tm_min=(int)((day_seconds/60)%60);
	result.tm_hour=(int)(day_seconds/3600);
	result.tm_mday=(int)days+1;
	result.tm_mon=month;
	result.tm_year=year-1900;
	result.tm_wday=(int)((seconds/86400+4)%7);
	result.tm_yday=yday;
	result.tm_isdst=0;
	return &result;
}

struct tm *localtime(const time_t *value){return gmtime(value);}

int
gettimeofday(struct timeval *value, void *timezone)
{
	struct timespec now;
	(void)timezone;
	if (value == NULL) {
		errno = EFAULT;
		return -1;
	}
	if (clock_gettime(CLOCK_REALTIME, &now) != 0)
		return -1;
	value->tv_sec = now.tv_sec;
	value->tv_usec = now.tv_nsec / 1000;
	return 0;
}

int
clock_gettime(clockid_t clock, struct timespec *value)
{
	return (int)__libos_call(SYSCALL_CLOCK_GETTIME, clock, (long)value,
	    0, 0, 0, 0);
}

time_t
time(time_t *result)
{
	struct timespec now;
	if (clock_gettime(CLOCK_REALTIME, &now) != 0)
		return (time_t)-1;
	if (result != NULL)
		*result = now.tv_sec;
	return now.tv_sec;
}

int
nanosleep(const struct timespec *request, struct timespec *remaining)
{
	return (int)__libos_call(SYSCALL_NANOSLEEP, (long)request,
	    (long)remaining, 0, 0, 0, 0);
}

unsigned int
sleep(unsigned int seconds)
{
	struct timespec request = { .tv_sec = seconds, .tv_nsec = 0 };
	struct timespec remaining = {0};
	if (nanosleep(&request, &remaining) == 0)
		return 0;
	if (errno == EINTR)
		return (unsigned int)remaining.tv_sec +
		    (remaining.tv_nsec != 0);
	return seconds;
}
