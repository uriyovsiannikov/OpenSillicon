#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <time.h>

static __thread volatile uint64_t clock_tls = 0x434c4f434b544c53ULL;

static long
sleep_ticks(uint64_t ticks)
{
	long result = 13;
	unsigned char failed;

	__asm__ volatile ("int $0x80\n\tsetc %0"
	    : "=m" (failed), "+a" (result)
	    : "D" (ticks)
	    : "rcx", "r11", "memory", "cc");
	return failed ? -result : result;
}

static int
valid_timespec(const struct timespec *value)
{
	return value->tv_sec >= 0 && value->tv_nsec >= 0 &&
	    value->tv_nsec < 1000000000L;
}

static int
not_earlier(const struct timespec *left, const struct timespec *right)
{
	return right->tv_sec > left->tv_sec ||
	    (right->tv_sec == left->tv_sec && right->tv_nsec >= left->tv_nsec);
}

int
main(int argc, char **argv, char **envp)
{
	struct timespec first;
	struct timespec second;
	struct timespec third;

	if (argc != 0 || argv == NULL || argv[0] != NULL || envp == NULL ||
	    envp[0] != NULL || clock_tls != 0x434c4f434b544c53ULL)
		return 21;
	if (clock_gettime(CLOCK_MONOTONIC, &first) != 0 ||
	    !valid_timespec(&first))
		return 22;
	if (clock_gettime(CLOCK_MONOTONIC, &second) != 0 ||
	    !valid_timespec(&second) || !not_earlier(&first, &second))
		return 23;
	if (sleep_ticks(2) != 0 ||
	    clock_gettime(CLOCK_MONOTONIC, &third) != 0 ||
	    !valid_timespec(&third) || !not_earlier(&second, &third) ||
	    (third.tv_sec == second.tv_sec && third.tv_nsec == second.tv_nsec) ||
	    clock_tls != 0x434c4f434b544c53ULL)
		return 24;
	errno = 0;
	if (clock_gettime(CLOCK_REALTIME, &first) != -1 || errno != EINVAL)
		return 25;
	errno = 0;
	if (clock_gettime(CLOCK_MONOTONIC, (struct timespec *)(uintptr_t)1) != -1 ||
	    errno != EFAULT)
		return 26;
	errno = 0;
	if (clock_gettime(CLOCK_MONOTONIC, NULL) != -1 || errno != EFAULT)
		return 27;
	return 0;
}
