#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <time.h>

static volatile int caught_signal;
static __thread volatile uint64_t nanosleep_tls = 0x4e414e4f534c4545ULL;

struct os_sigaction { uint64_t handler; uint64_t restorer; };

static long
native_call(long number, long a1, long a2, long a3)
{
	unsigned char failed;
	__asm__ volatile ("int $0x80\n\tsetc %0"
	    : "=m" (failed), "+a" (number)
	    : "D" (a1), "S" (a2), "d" (a3)
	    : "rcx", "r11", "memory", "cc");
	return failed ? -number : number;
}

static void
signal_handler(int signo)
{
	caught_signal = signo;
}

__attribute__((naked)) static void
signal_restorer(void)
{
	__asm__ volatile ("mov $17, %eax\n\tint $0x80\n\tud2");
}

static int
not_earlier(const struct timespec *a, const struct timespec *b)
{
	return b->tv_sec > a->tv_sec ||
	    (b->tv_sec == a->tv_sec && b->tv_nsec >= a->tv_nsec);
}

int
main(int argc, char **argv, char **envp)
{
	struct timespec before, after, request;
	struct os_sigaction action;

	if (argc != 0 || argv == 0 || argv[0] != 0 || envp == 0 || envp[0] != 0 ||
	    nanosleep_tls != 0x4e414e4f534c4545ULL)
		return 21;
	request = (struct timespec){ 0, 0 };
	if (nanosleep(&request, 0) != 0)
		return 22;
	request = (struct timespec){ 0, 15000000 };
	if (clock_gettime(CLOCK_MONOTONIC, &before) != 0 ||
	    nanosleep(&request, 0) != 0 ||
	    clock_gettime(CLOCK_MONOTONIC, &after) != 0 ||
	    !not_earlier(&before, &after) ||
	    (after.tv_sec == before.tv_sec && after.tv_nsec - before.tv_nsec < 15000000L))
		return 23;
	request = (struct timespec){ 0, 10000000 };
	if (nanosleep(&request, (struct timespec *)(uintptr_t)1) != -1 ||
	    errno != EFAULT)
		return 24;
	if (nanosleep((const struct timespec *)(uintptr_t)1, 0) != -1 ||
	    errno != EFAULT)
		return 25;
	request = (struct timespec){ -1, 0 };
	if (nanosleep(&request, 0) != -1 || errno != EINVAL)
		return 26;
	request = (struct timespec){ 0, -1 };
	if (nanosleep(&request, 0) != -1 || errno != EINVAL)
		return 27;
	request = (struct timespec){ 0, 1000000000L };
	if (nanosleep(&request, 0) != -1 || errno != EINVAL)
		return 28;
	request = (struct timespec){ 0, 999999999L };
	if (nanosleep(&request, 0) != 0)
		return 29;
	/* Exercise the already-supported native fork/signal path directly; these
	 * calls are test orchestration and are not added to the musl adapter. */
	action.handler = (uint64_t)(uintptr_t)signal_handler;
	action.restorer = (uint64_t)(uintptr_t)signal_restorer;
	if (native_call(16, 2, (long)&action, 0) != 0)
		return 30;
	{
		long parent = native_call(3, 0, 0, 0);
		long child = native_call(4, 0, 0, 0);
		if (child == 0) {
			(void)native_call(13, 2, 0, 0);
			(void)native_call(15, parent, 2, 0);
			(void)native_call(1, 0, 0, 0);
			__builtin_unreachable();
		}
		if (child < 0)
			return 31;
		{
			request = (struct timespec){ 1, 0 };
			if (nanosleep(&request, &after) != -1 || errno != EINTR ||
			    caught_signal != 2 || after.tv_sec < 0 ||
			    after.tv_nsec < 0 || after.tv_nsec >= 1000000000L ||
			    after.tv_sec > 1 || (after.tv_sec == 1 && after.tv_nsec != 0))
				return 32;
			(void)native_call(6, child, 0, 0);
		}
	}
	return nanosleep_tls == 0x4e414e4f534c4545ULL ? 0 : 33;
}
