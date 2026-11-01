#include "include/os.h"

#define SHM_TEST_EAGAIN 11
#define SHM_TEST_PAGE_SIZE 4096ULL
#define SHM_TEST_FIRST_BASE (0x0000700000001000ULL)

int
user_main(void)
{
	long first = u_shm_create(SHM_TEST_PAGE_SIZE);
	long second = u_shm_create(SHM_TEST_PAGE_SIZE);
	volatile uint64_t *one;
	volatile uint64_t *two;
	long child;

	if (first != (long)SHM_TEST_FIRST_BASE ||
	    second != first + 2 * (long)SHM_TEST_PAGE_SIZE)
		u_exit(20);
	one = (volatile uint64_t *)(uintptr_t)first;
	two = (volatile uint64_t *)(uintptr_t)second;
	*one = 0x1111;
	*two = 0x2222;
	child = u_fork();
	if (child != -SHM_TEST_EAGAIN || *one != 0x1111 || *two != 0x2222)
		u_exit(21);
	*one = 0x3333;
	*two = 0x4444;
	if (*one != 0x3333 || *two != 0x4444 ||
	    u_shm_detach((uint64_t)first) != 0 ||
	    u_shm_detach((uint64_t)second) != 0)
		u_exit(22);
	u_puts("OpenSillicon: partial shared fork mapping rollback passed\n");
	return 0;
}
