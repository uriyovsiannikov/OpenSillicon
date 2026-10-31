#include "include/os.h"

static volatile uint64_t received_signal;
static volatile uint64_t fork_result_in_handler;
static volatile uint64_t fork_in_handler;
static volatile uint64_t forge_sigreturn_in_handler;

static long
raw_sigreturn(void)
{
	uint64_t result = 17;
	unsigned char failed;

	__asm__ volatile ("int $128; setc %1" : "+a" (result), "=qm" (failed) :
	    : "rcx", "r11", "memory");
	return failed ? -(long)result : (long)result;
}

__attribute__((naked, noreturn)) static void
signal_with_invalid_stack(uint64_t pid __attribute__((unused)))
{
	__asm__ ("mov $2, %esi\n"
	    "mov $15, %eax\n"
	    "mov $0x10008, %rsp\n"
	    "int $128\n"
	    "ud2\n");
}

static void
signal_handler(uint64_t signo)
{
	received_signal = signo;
	if (fork_in_handler != 0)
		fork_result_in_handler = (uint64_t)u_fork();
	if (forge_sigreturn_in_handler != 0)
		(void)raw_sigreturn();
}

static int
wait_for_status(uint64_t pid, uint8_t expected)
{
	uint8_t status = 0;

	return u_waitpid(pid, &status) == (long)pid && status == expected;
}

static void
signal_parent_after_delay(uint64_t parent_pid, uint64_t trailing_ticks)
{
	(void)u_sleep_ticks(2);
	(void)u_kill(parent_pid, U_SIGINT);
	if (trailing_ticks != 0)
		(void)u_sleep_ticks(trailing_ticks);
	u_exit(0);
}

static int
test_waitpid_interrupt(uint64_t parent_pid)
{
	long child = u_fork();
	long result;

	if (child == 0)
		signal_parent_after_delay(parent_pid, 5);
	if (child < 0)
		return 0;
	received_signal = 0;
	result = u_waitpid((uint64_t)child, 0);
	if (result != -4 || received_signal != U_SIGINT ||
	    !wait_for_status((uint64_t)child, 0))
		return 0;
	return 1;
}

static int
test_sleep_interrupt(uint64_t parent_pid)
{
	long child = u_fork();
	long result;

	if (child == 0)
		signal_parent_after_delay(parent_pid, 0);
	if (child < 0)
		return 0;
	received_signal = 0;
	result = u_sleep_ticks(100);
	if (result != -4 || received_signal != U_SIGINT ||
	    !wait_for_status((uint64_t)child, 0))
		return 0;
	return 1;
}

static int
test_ignored_signal_does_not_interrupt(uint64_t parent_pid)
{
	long child = u_fork();
	long result;

	if (child == 0) {
		(void)u_sleep_ticks(2);
		(void)u_kill(parent_pid, U_SIGTERM);
		u_exit(0);
	}
	if (child < 0)
		return 0;
	received_signal = 0;
	result = u_sleep_ticks(5);
	if (result != 0 || received_signal != 0 ||
	    !wait_for_status((uint64_t)child, 0))
		return 0;
	return 1;
}

static int
test_pipe_read_interrupt(uint64_t parent_pid)
{
	uint32_t descriptors[2];
	uint8_t byte;
	long child;
	long result;

	if (u_pipe(descriptors) != 0)
		return 0;
	child = u_fork();
	if (child == 0) {
		(void)u_close(descriptors[0]);
		signal_parent_after_delay(parent_pid, 0);
	}
	if (child < 0)
		return 0;
	(void)u_close(descriptors[1]);
	received_signal = 0;
	result = u_read(descriptors[0], &byte, 1);
	if (result != -4 || received_signal != U_SIGINT ||
	    u_close(descriptors[0]) != 0 || !wait_for_status((uint64_t)child, 0))
		return 0;
	return 1;
}

static int
test_pipe_read_completion_race(uint64_t parent_pid)
{
	uint32_t descriptors[2];
	uint8_t byte = 0;
	long child;
	long result;

	if (u_pipe(descriptors) != 0)
		return 0;
	child = u_fork();
	if (child == 0) {
		uint8_t sent = 'r';

		(void)u_close(descriptors[0]);
		(void)u_sleep_ticks(2);
		if (u_write(descriptors[1], &sent, 1) != 1)
			u_exit(1);
		(void)u_kill(parent_pid, U_SIGINT);
		u_exit(0);
	}
	if (child < 0)
		return 0;
	(void)u_close(descriptors[1]);
	received_signal = 0;
	result = u_read(descriptors[0], &byte, 1);
	if (result != 1 || byte != 'r' || received_signal != U_SIGINT ||
	    !wait_for_status((uint64_t)child, 0) ||
	    u_close(descriptors[0]) != 0)
		return 0;
	return 1;
}

static int
test_pipe_write_interrupt(uint64_t parent_pid)
{
	static uint8_t fill[1024];
	static uint8_t drained[4096];
	uint32_t descriptors[2];
	uint8_t byte = 'z';
	long child;
	long result;
	size_t i;

	for (i = 0; i < sizeof(fill); i++)
		fill[i] = 'x';
	if (u_pipe(descriptors) != 0)
		return 0;
	for (i = 0; i < 4; i++) {
		if (u_write(descriptors[1], fill, sizeof(fill)) != sizeof(fill))
			return 0;
	}
	child = u_fork();
	if (child == 0) {
		(void)u_close(descriptors[1]);
		signal_parent_after_delay(parent_pid, 0);
	}
	if (child < 0)
		return 0;
	received_signal = 0;
	result = u_write(descriptors[1], &byte, 1);
	if (result != -4 || received_signal != U_SIGINT ||
	    u_read(descriptors[0], drained, sizeof(drained)) != sizeof(drained))
		return 0;
	for (i = 0; i < sizeof(drained); i++) {
		if (drained[i] != 'x')
			return 0;
	}
	if (u_write(descriptors[1], &byte, 1) != 1 ||
	    u_read(descriptors[0], &byte, 1) != 1 || byte != 'z' ||
	    !wait_for_status((uint64_t)child, 0))
		return 0;
	(void)u_close(descriptors[0]);
	(void)u_close(descriptors[1]);
	return 1;
}

static int
test_pipe_write_completion_race(uint64_t parent_pid)
{
	static uint8_t fill[1024];
	static uint8_t drained[4096];
	uint32_t descriptors[2];
	uint8_t byte = 'z';
	long child;
	long result;
	size_t i;

	for (i = 0; i < sizeof(fill); i++)
		fill[i] = 'x';
	if (u_pipe(descriptors) != 0)
		return 0;
	for (i = 0; i < 4; i++) {
		if (u_write(descriptors[1], fill, sizeof(fill)) != sizeof(fill))
			return 0;
	}
	child = u_fork();
	if (child == 0) {
		uint8_t discarded;

		(void)u_close(descriptors[1]);
		(void)u_sleep_ticks(2);
		if (u_read(descriptors[0], &discarded, 1) != 1)
			u_exit(1);
		(void)u_kill(parent_pid, U_SIGINT);
		u_exit(0);
	}
	if (child < 0)
		return 0;
	received_signal = 0;
	result = u_write(descriptors[1], &byte, 1);
	if (result != 1 || received_signal != U_SIGINT ||
	    u_read(descriptors[0], drained, sizeof(drained)) != sizeof(drained))
		return 0;
	for (i = 0; i < sizeof(drained) - 1; i++) {
		if (drained[i] != 'x')
			return 0;
	}
	if (drained[sizeof(drained) - 1] != 'z' ||
	    !wait_for_status((uint64_t)child, 0))
		return 0;
	(void)u_close(descriptors[0]);
	(void)u_close(descriptors[1]);
	return 1;
}

int
user_main(void)
{
	struct u_sigaction action = {
		.handler = (uintptr_t)signal_handler,
		.restorer = (uintptr_t)u_sigreturn
	};
	struct u_sigaction ignored = {U_SIG_IGN, 0};
	struct u_sigaction defaults = {U_SIG_DFL, 0};
	struct u_sigaction old;
	long child;
	long result;
	uint64_t parent_pid = (uint64_t)u_getpid();

	if (u_sigaction(U_SIGINT, &action, 0) != 0) {
		u_puts("OpenSillicon: caught signal delivery test failed\n");
		return 1;
	}
	fork_in_handler = 1;
	result = u_kill((uint64_t)u_getpid(), U_SIGINT);
	fork_in_handler = 0;
	if (result != 0) {
		u_puts("OpenSillicon: kill failed errno ");
		u_put_u64((uint64_t)-result);
		u_puts("\n");
		return 2;
	}
	if (received_signal != U_SIGINT || (long)fork_result_in_handler != -16) {
		u_puts("OpenSillicon: caught signal delivery test failed\n");
		return 3;
	}
	if (raw_sigreturn() != -22) {
		u_puts("OpenSillicon: sigreturn without handler failed\n");
		return 17;
	}
	child = u_fork();
	if (child == 0) {
		forge_sigreturn_in_handler = 1;
		(void)u_kill((uint64_t)u_getpid(), U_SIGINT);
		u_exit(1);
	}
	if (child < 0 || !wait_for_status((uint64_t)child, 142)) {
		u_puts("OpenSillicon: forged sigreturn was not rejected\n");
		return 18;
	}
	child = u_fork();
	if (child == 0)
		signal_with_invalid_stack((uint64_t)u_getpid());
	if (child < 0 || !wait_for_status((uint64_t)child, 142)) {
		u_puts("OpenSillicon: invalid signal stack was not rejected\n");
		return 19;
	}
	if (u_sigaction(U_SIGKILL, &ignored, 0) != -22 ||
	    u_sigaction(U_SIGKILL, &action, 0) != -22 ||
	    u_sigaction(U_SIGINT, &action, (struct u_sigaction *)(uintptr_t)0x10000)
	    != -14 || u_sigaction(U_SIGINT, 0, &old) != 0 ||
	    old.handler != action.handler ||
	    u_sigaction(U_SIGINT, &action,
	    (struct u_sigaction *)(uintptr_t)0x10000) != -14 ||
	    u_sigaction(U_SIGINT, 0, &old) != 0 || old.handler != action.handler ||
	    u_sigaction(U_SIGINT, (const struct u_sigaction *)(uintptr_t)0x10000,
	    0) != -14) {
		u_puts("OpenSillicon: signal action validation test failed\n");
		return 4;
	}
	{
		struct u_sigaction invalid = {
			.handler = 0x10000,
			.restorer = (uintptr_t)u_sigreturn
		};
		struct u_sigaction bad_restorer = {
			.handler = (uintptr_t)signal_handler,
			.restorer = (uintptr_t)signal_handler
		};

		if (u_sigaction(U_SIGINT, &invalid, 0) != -22 ||
		    u_sigaction(U_SIGINT, &bad_restorer, 0) != -22) {
			u_puts("OpenSillicon: executable handler validation failed\n");
			return 5;
		}
	}
	if (u_kill(0, U_SIGINT) != -22 ||
	    u_kill((uint64_t)u_getpid() + 100000, U_SIGINT) != -3 ||
	    u_kill((uint64_t)u_getpid(), 0) != -22 ||
	    u_kill((uint64_t)u_getpid(), (1ULL << 32) + U_SIGINT) != -22 ||
	    u_sigaction((1ULL << 32) + U_SIGINT, &defaults, 0) != -22) {
		u_puts("OpenSillicon: kill target validation failed\n");
		return 6;
	}
	if (u_sigaction(U_SIGTERM, &ignored, 0) != 0 ||
	    u_kill((uint64_t)u_getpid(), U_SIGTERM) != 0) {
		u_puts("OpenSillicon: ignored signal test failed\n");
		return 7;
	}
	child = u_fork();
	if (child == 0) {
		(void)u_kill((uint64_t)u_getpid(), U_SIGTERM);
		u_exit(0);
	}
	if (child < 0 || !wait_for_status((uint64_t)child, 0)) {
		u_puts("OpenSillicon: fork inherited ignored disposition failed\n");
		return 8;
	}
	if (u_sigaction(U_SIGTERM, &defaults, 0) != 0)
		return 9;
	child = u_fork();
	if (child == 0) {
		(void)u_kill((uint64_t)u_getpid(), U_SIGTERM);
		u_exit(1);
	}
	if (child < 0 || !wait_for_status((uint64_t)child, 143)) {
		u_puts("OpenSillicon: default signal termination failed\n");
		return 10;
	}
	if (u_sigaction(U_SIGKILL, &defaults, 0) != 0)
		return 11;
	child = u_fork();
	if (child == 0) {
		(void)u_sleep_ticks(20);
		u_exit(0);
	}
	if (child < 0 || u_sleep_ticks(2) != 0 ||
	    u_kill((uint64_t)child, U_SIGKILL) != 0 ||
	    !wait_for_status((uint64_t)child, 137)) {
		u_puts("OpenSillicon: uncatchable signal termination failed\n");
		return 12;
	}
	if (u_sigaction(U_SIGINT, &action, 0) != 0 ||
	    u_sigaction(U_SIGTERM, &ignored, 0) != 0)
		return 13;
	child = u_fork();
	if (child == 0) {
		result = u_exec_path("/not-present");
		if (result != -2 || u_sigaction(U_SIGINT, 0, &old) != 0 ||
		    old.handler != action.handler)
			u_exit(14);
		u_exit(0);
	}
	if (child < 0 || !wait_for_status((uint64_t)child, 0)) {
		u_puts("OpenSillicon: failed exec changed signal state\n");
		return 14;
	}
	child = u_fork();
	if (child == 0) {
		(void)u_exec_path("/init/bin/signal_exec_child");
		u_exit(15);
	}
	if (child < 0 || !wait_for_status((uint64_t)child, 0)) {
		u_puts("OpenSillicon: successful exec signal state failed\n");
		return 15;
	}
	if (u_sigaction(U_SIGINT, &action, 0) != 0)
		return 16;
	if (!test_waitpid_interrupt(parent_pid)) {
		u_puts("OpenSillicon: waitpid signal interruption failed\n");
		return 16;
	}
	if (!test_sleep_interrupt(parent_pid)) {
		u_puts("OpenSillicon: sleep signal interruption failed\n");
		return 16;
	}
	if (u_sigaction(U_SIGTERM, &ignored, 0) != 0 ||
	    !test_ignored_signal_does_not_interrupt(parent_pid)) {
		u_puts("OpenSillicon: ignored signal interrupted sleep\n");
		return 16;
	}
	if (u_sigaction(U_SIGTERM, &defaults, 0) != 0)
		return 16;
	if (!test_pipe_read_interrupt(parent_pid)) {
		u_puts("OpenSillicon: pipe read signal interruption failed\n");
		return 16;
	}
	if (!test_pipe_read_completion_race(parent_pid)) {
		u_puts("OpenSillicon: pipe read completion race failed\n");
		return 16;
	}
	if (!test_pipe_write_interrupt(parent_pid)) {
		u_puts("OpenSillicon: pipe write signal interruption failed\n");
		return 16;
	}
	if (!test_pipe_write_completion_race(parent_pid)) {
		u_puts("OpenSillicon: pipe write completion race failed\n");
		return 16;
	}
	return 0;
}
