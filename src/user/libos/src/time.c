#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "abi/syscall.h"
#include "internal.h"

static long floor_div(long a, long b)
{ long q=a/b; return q-(a%b<0); }

struct tm *
gmtime(const time_t *value)
{
    static struct tm result;
    if (!value) return NULL;
    long days=floor_div(*value,86400);
    long sec=*value%86400;
    if (sec<0) sec+=86400;
    /* Gregorian civil date from days since 1970-01-01. */
    long z=days+719468, era=floor_div(z,146097);
    unsigned doe=(unsigned)(z-era*146097);
    unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    long year=(long)yoe+era*400;
    unsigned doy=doe-(365*yoe+yoe/4-yoe/100);
    unsigned mp=(5*doy+2)/153;
    unsigned day=doy-(153*mp+2)/5+1;
    int month=(int)mp+(mp<10?3:-9);
    year+=(month<=2);
    if (year-1900<INT_MIN || year-1900>INT_MAX) {
        errno=EOVERFLOW; return NULL;
    }
    result.tm_year=(int)(year-1900); result.tm_mon=month-1;
    result.tm_mday=(int)day;
    result.tm_sec=(int)(sec%60); result.tm_min=(int)(sec/60%60);
    result.tm_hour=(int)(sec/3600);
    long weekday=(days+4)%7;
    result.tm_wday=(int)(weekday<0?weekday+7:weekday);
    static const int before[]={0,31,59,90,120,151,181,212,243,273,304,334};
    int leap=year%4==0 && (year%100!=0 || year%400==0);
    result.tm_yday=before[month-1]+(int)day-1+(month>2 && leap);
    result.tm_isdst=0;
    return &result;
}

time_t
mktime(struct tm *t)
{
    if (!t) { errno=EINVAL; return (time_t)-1; }
    long year=(long)t->tm_year+1900;
    long mon=t->tm_mon;
    year+=floor_div(mon,12); mon-=floor_div(mon,12)*12;
    year-=mon<2;
    long era=floor_div(year,400);
    unsigned yoe=(unsigned)(year-era*400);
    unsigned mp=(unsigned)(mon+(mon>1?-2:10));
    long days=era*146097+(long)(yoe*365+yoe/4-yoe/100+(153*mp+2)/5)
        +(long)t->tm_mday-1-719468;
    time_t seconds=days*86400+(long)t->tm_hour*3600+
        (long)t->tm_min*60+t->tm_sec;
    struct tm *normalized=gmtime(&seconds);
    if (!normalized) return (time_t)-1;
    *t=*normalized;
    return seconds;
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
