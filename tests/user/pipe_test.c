#include "include/os.h"

#define PIPE_TEST_EFAULT 14
#define PIPE_TEST_EMFILE 24
#define PIPE_TEST_EPIPE 32

static char drain_buffer[4096];

static int
bytes_equal(const char *left, const char *right, size_t length)
{
	size_t i;
	for (i = 0; i < length; i++) {
		if (left[i] != right[i])
			return 0;
	}
	return 1;
}

static void
fail(uint64_t status)
{
	u_puts("OpenSillicon: pipe userspace test failed\n");
	u_exit(status);
}

static int
test_fork_data_eof(void)
{
	static const char message[] = "parent-child-pipe";
	uint32_t descriptors[2];
	char buffer[64];
	uint8_t status = 0xff;
	long child;
	long amount;

	if (u_pipe(descriptors) != 0)
		return 1;
	child = u_fork();
	if (child < 0)
		return 2;
	if (child == 0) {
		(void)u_close(descriptors[0]);
		if (u_write(descriptors[1], message, sizeof(message) - 1) !=
		    (long)(sizeof(message) - 1))
			u_exit(3);
		/* Exit must release the final write-end reference. */
		u_exit(0);
	}
	(void)u_close(descriptors[1]);
	amount = u_read(descriptors[0], buffer, sizeof(buffer));
	if (amount != (long)(sizeof(message) - 1) ||
	    !bytes_equal(buffer, message, sizeof(message) - 1))
		return 4;
	if (u_read(descriptors[0], buffer, sizeof(buffer)) != 0)
		return 5;
	if (u_waitpid((uint64_t)child, &status) != child || status != 0)
		return 6;
	(void)u_close(descriptors[0]);
	return 0;
}

static int
test_exec_inheritance(void)
{
	uint32_t descriptors[2];
	char buffer[64];
	static const char expected[] = "exec-inherited-pipe";
	uint8_t status = 0xff;
	long child;
	long amount;

	if (u_pipe(descriptors) != 0)
		return 10;
	child = u_fork();
	if (child < 0)
		return 11;
	if (child == 0) {
		(void)u_close(descriptors[0]);
		(void)u_exec_path("/init/bin/pipe_exec_child");
		u_exit(12);
	}
	(void)u_close(descriptors[1]);
	amount = u_read(descriptors[0], buffer, sizeof(buffer));
	if (amount != (long)(sizeof(expected) - 1) ||
	    !bytes_equal(buffer, expected, sizeof(expected) - 1))
		return 13;
	if (u_read(descriptors[0], buffer, sizeof(buffer)) != 0)
		return 14;
	if (u_waitpid((uint64_t)child, &status) != child || status != 0)
		return 15;
	(void)u_close(descriptors[0]);
	return 0;
}

static int
test_invalid_copy_and_broken_pipe(void)
{
	uint32_t descriptors[2];
	uint32_t *bad_descriptors = (uint32_t *)(uintptr_t)0x1000;
	uint32_t *cross_page_descriptors =
	    (uint32_t *)(uintptr_t)0x7fffffffdffcULL;
	static const uint32_t readonly_descriptors[2] = {0, 0};
	char *bad_buffer = (char *)(uintptr_t)0x1000;
	char buffer[8];
	static const char message[] = "safe";

	if (u_pipe(bad_descriptors) != -PIPE_TEST_EFAULT)
		return 20;
	if (u_pipe((uint32_t *)(uintptr_t)readonly_descriptors) !=
	    -PIPE_TEST_EFAULT)
		return 21;
	if (u_pipe(cross_page_descriptors) != -PIPE_TEST_EFAULT)
		return 22;
	if (u_pipe(descriptors) != 0)
		return 23;
	if (u_write(descriptors[1], bad_buffer, 1) != -PIPE_TEST_EFAULT)
		return 24;
	if (u_write(descriptors[1], message, sizeof(message) - 1) !=
	    (long)(sizeof(message) - 1))
		return 25;
	if (u_read(descriptors[0], bad_buffer, sizeof(message) - 1) !=
	    -PIPE_TEST_EFAULT)
		return 26;
	if (u_read(descriptors[0], buffer, sizeof(buffer)) !=
	    (long)(sizeof(message) - 1) ||
	    !bytes_equal(buffer, message, sizeof(message) - 1))
		return 27;
	(void)u_close(descriptors[0]);
	if (u_write(descriptors[1], message, 1) != -PIPE_TEST_EPIPE)
		return 28;
	(void)u_close(descriptors[1]);
	return 0;
}

static int
test_blocked_reader(void)
{
	uint32_t descriptors[2];
	char byte = 0;
	uint8_t status = 0xff;
	static const char expected = 'R';
	long child;

	if (u_pipe(descriptors) != 0)
		return 30;
	child = u_fork();
	if (child < 0)
		return 31;
	if (child == 0) {
		(void)u_close(descriptors[0]);
		if (u_sleep_ticks(3) != 0 || u_write(descriptors[1], &expected, 1) != 1)
			u_exit(32);
		(void)u_close(descriptors[1]);
		u_exit(0);
	}
	(void)u_close(descriptors[1]);
	if (u_read(descriptors[0], &byte, 1) != 1 || byte != expected)
		return 33;
	if (u_waitpid((uint64_t)child, &status) != child || status != 0)
		return 34;
	(void)u_close(descriptors[0]);
	return 0;
}

static int
test_blocked_writer(void)
{
	uint32_t descriptors[2];
	static char fill[4096];
	char extra = 'W';
	char result = 0;
	uint8_t status = 0xff;
	long child;
	unsigned int i;

	for (i = 0; i < sizeof(fill); i++)
		fill[i] = 'F';
	if (u_pipe(descriptors) != 0 ||
	    u_write(descriptors[1], fill, sizeof(fill)) != sizeof(fill))
		return 40;
	child = u_fork();
	if (child < 0)
		return 41;
	if (child == 0) {
		(void)u_close(descriptors[1]);
		if (u_sleep_ticks(3) != 0 ||
		    u_read(descriptors[0], drain_buffer, sizeof(drain_buffer)) !=
		    sizeof(drain_buffer))
			u_exit(42);
		(void)u_close(descriptors[0]);
		u_exit(0);
	}
	if (u_write(descriptors[1], &extra, 1) != 1)
		return 43;
	if (u_read(descriptors[0], &result, 1) != 1 || result != extra)
		return 44;
	if (u_waitpid((uint64_t)child, &status) != child || status != 0)
		return 45;
	(void)u_close(descriptors[0]);
	(void)u_close(descriptors[1]);
	return 0;
}

int
user_main(void)
{
	uint32_t descriptors[2];
	uint32_t held[14][2];
	unsigned int i;
	int result;

	if (u_pipe(NULL) != -PIPE_TEST_EFAULT)
		fail(50);
	for (i = 0; i < 14; i++) {
		if (u_pipe(held[i]) != 0)
			fail(51);
	}
	if (u_pipe(descriptors) != -PIPE_TEST_EMFILE)
		fail(52);
	u_puts("OpenSillicon: pipe descriptor rollback passed\n");
	for (i = 0; i < 14; i++) {
		(void)u_close(held[i][0]);
		(void)u_close(held[i][1]);
	}
	result = test_fork_data_eof();
	if (result == 0) u_puts("OpenSillicon: pipe fork and EOF passed\n");
	if (result == 0) result = test_exec_inheritance();
	if (result == 0) u_puts("OpenSillicon: pipe exec inheritance passed\n");
	if (result == 0) result = test_invalid_copy_and_broken_pipe();
	if (result == 0) u_puts("OpenSillicon: pipe fault and EPIPE passed\n");
	if (result == 0) result = test_blocked_reader();
	if (result == 0) u_puts("OpenSillicon: blocked pipe reader woke\n");
	if (result == 0) result = test_blocked_writer();
	if (result == 0) u_puts("OpenSillicon: blocked pipe writer woke\n");
	if (result != 0)
		fail((uint64_t)result);
	u_puts("OpenSillicon: pipe fork, exec, EOF, EPIPE, faults, and blocking passed\n");
	u_exit(0);
}
