#include "include/os.h"

#define SHM_TEST_EAGAIN 11
#define SHM_TEST_ENOENT 2
#define SHM_TEST_PAGE_SIZE 4096ULL
#define SHM_TEST_FIRST_BASE (0x0000700000001000ULL)

static volatile uint64_t *shared_word(uint64_t base)
{
	return (volatile uint64_t *)(uintptr_t)base;
}

static int
wait_success(long pid)
{
	uint8_t status = 0xff;

	return u_waitpid((uint64_t)pid, &status) == pid && status == 0;
}

static int
test_fork_generations_and_detach(uint64_t base)
{
	long child = u_fork();

	if (child < 0)
		return 1;
	if (child == 0) {
		long grandchild;

		if (*shared_word(base) != 0x1111222233334444ULL)
			u_exit(2);
		*shared_word(base) = 0x5555666677778888ULL;
		grandchild = u_fork();
		if (grandchild < 0)
			u_exit(3);
		if (grandchild == 0) {
			if (*shared_word(base) != 0x5555666677778888ULL)
				u_exit(4);
			*shared_word(base) = 0x9999aaaabbbbccccULL;
			u_exit(0);
		}
		if (!wait_success(grandchild) ||
		    *shared_word(base) != 0x9999aaaabbbbccccULL ||
		    u_shm_detach(base) != 0)
			u_exit(5);
		u_exit(0);
	}
	if (!wait_success(child) ||
	    *shared_word(base) != 0x9999aaaabbbbccccULL)
		return 6;
	*shared_word(base) = 0x123456789abcdef0ULL;
	child = u_fork();
	if (child < 0)
		return 7;
	if (child == 0)
		u_exit(*shared_word(base) == 0x123456789abcdef0ULL ? 0 : 8);
	if (!wait_success(child) ||
	    *shared_word(base) != 0x123456789abcdef0ULL)
		return 9;
	return 0;
}

static int
test_failed_exec_and_cursor(uint64_t base)
{
	long next;

	if (u_exec_path("/init/bin/no-such-executable") != -SHM_TEST_ENOENT ||
	    *shared_word(base) != 0x123456789abcdef0ULL)
		return 10;
	next = u_shm_create(1);
	if (next != (long)(base + 2 * SHM_TEST_PAGE_SIZE) ||
	    u_shm_detach((uint64_t)next) != 0)
		return 11;
	return 0;
}

static int
test_zombie_retention(uint64_t base)
{
	long child = u_fork();

	if (child < 0)
		return 12;
	if (child == 0) {
		*shared_word(base) = 0x0ddc0ffeebadf00dULL;
		u_exit(0);
	}
	if (u_shm_detach(base) != 0 || u_sleep_ticks(3) != 0 ||
	    !wait_success(child))
		return 13;
	return 0;
}

static int
test_exec_releases_mapping(uint64_t base)
{
	long child = u_fork();

	if (child < 0)
		return 14;
	if (child == 0) {
		(void)u_exec_path("/init/bin/exec_path_child");
		u_exit(15);
	}
	if (u_shm_detach(base) != 0 || !wait_success(child))
		return 16;
	return 0;
}

int
user_main(void)
{
	long base;
	long next;
	int result;

	base = u_shm_create(SHM_TEST_PAGE_SIZE);
	if (base != (long)SHM_TEST_FIRST_BASE)
		u_exit(20);
	*shared_word((uint64_t)base) = 0x1111222233334444ULL;
	result = test_fork_generations_and_detach((uint64_t)base);
	if (result != 0)
		u_exit((uint64_t)result);
	result = test_failed_exec_and_cursor((uint64_t)base);
	if (result != 0)
		u_exit((uint64_t)result);
	next = u_shm_create(SHM_TEST_PAGE_SIZE);
	if (next != base + 4 * (long)SHM_TEST_PAGE_SIZE)
		u_exit(17);
	result = test_zombie_retention((uint64_t)next);
	if (result != 0)
		u_exit((uint64_t)result);
	next = u_shm_create(SHM_TEST_PAGE_SIZE);
	if (next != base + 6 * (long)SHM_TEST_PAGE_SIZE)
		u_exit(18);
	result = test_exec_releases_mapping((uint64_t)next);
	if (result != 0)
		u_exit((uint64_t)result);
	u_puts("OpenSillicon: shared-memory fork generations, detach, zombie reap, exec, and failed exec passed\n");
	return 0;
}
