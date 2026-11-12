#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include "abi/credentials.h"
#include "abi/syscall.h"

/* Linux-host test entry: exercise the real freestanding libos implementation
 * with a mock native syscall and without linking the host's UID functions. */
__asm__(".global _start\n_start:\n"
    "andq $-16, %rsp\ncall main\nmovl %eax, %edi\nmovl $60, %eax\nsyscall\n");
int errno;
static int failure;
static unsigned int calls;
long __libos_call(long number, long pointer, long a, long b, long c, long d, long e)
{
	(void)a; (void)b; (void)c; (void)d; (void)e;
	calls++;
	if (number != SYSCALL_GETCREDENTIALS) {
		errno = ENOSYS;
		return -1;
	}
	if (failure) {
		errno = failure;
		return -1;
	}
	*(struct os_credentials *)(uintptr_t)pointer = (struct os_credentials){
		.real_uid = 123, .effective_uid = 456, .saved_uid = 789,
		.real_gid = 321, .effective_gid = 654, .saved_gid = 987
	};
	return 0;
}
#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
int main(void)
{
	uid_t real, effective, saved;
	gid_t group_real, group_effective, group_saved;
	errno = EBUSY;
	CHECK(getuid() == 123 && geteuid() == 456);
	CHECK(getgid() == 321 && getegid() == 654);
	CHECK(errno == EBUSY);
	CHECK(getresuid(&real, &effective, &saved) == 0);
	CHECK(real == 123 && effective == 456 && saved == 789);
	CHECK(getresgid(&group_real, &group_effective, &group_saved) == 0);
	CHECK(group_real == 321 && group_effective == 654 && group_saved == 987);
	const int errors[] = {EIO, ENOSYS, EFAULT};
	for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		failure = errors[i];
		CHECK(getuid() == (uid_t)-1 && errno == failure);
		CHECK(geteuid() == (uid_t)-1 && errno == failure);
		CHECK(getgid() == (gid_t)-1 && errno == failure);
		CHECK(getegid() == (gid_t)-1 && errno == failure);
		CHECK(getresuid(&real, &effective, &saved) == -1 && errno == failure);
		CHECK(real == 123 && effective == 456 && saved == 789);
		CHECK(getresgid(&group_real, &group_effective, &group_saved) == -1);
		CHECK(group_real == 321 && group_effective == 654 && group_saved == 987);
	}
	unsigned int before = calls;
	CHECK(getresuid(0, &effective, &saved) == -1 && errno == EFAULT);
	CHECK(getresgid(&group_real, 0, &group_saved) == -1 && errno == EFAULT);
	CHECK(calls == before);
	return 0;
}
