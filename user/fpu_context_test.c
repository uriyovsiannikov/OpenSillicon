#include <stdint.h>

#include "include/os.h"

static const uint64_t parent_pattern[2] = {
	UINT64_C(0x1029384756abcdef), UINT64_C(0xfedcba6547382910)
};
static const uint64_t child_pattern[2] = {
	UINT64_C(0x8877665544332211), UINT64_C(0x11aa22bb33cc44dd)
};

static void
set_xmm(const uint64_t pattern[2])
{
	__asm__ volatile ("movdqu (%0), %%xmm0" : : "r" (pattern) : "xmm0", "memory");
}

static int
xmm_matches(const uint64_t pattern[2])
{
	uint64_t observed[2];

	__asm__ volatile ("movdqu %%xmm0, (%0)" : : "r" (observed) : "memory");
	return observed[0] == pattern[0] && observed[1] == pattern[1];
}

static int
xmm_is_zero(void)
{
	static const uint64_t zero[2] = { 0, 0 };

	return xmm_matches(zero);
}

static uint32_t
read_mxcsr(void)
{
	uint32_t value;

	__asm__ volatile ("stmxcsr %0" : "=m" (value));
	return value;
}

static int
read_initial_control_state(void)
{
	uint16_t x87_control;
	uint32_t mxcsr;

	__asm__ volatile ("fnstcw %0; stmxcsr %1" : "=m" (x87_control), "=m" (mxcsr));
	return x87_control == 0x037fU && mxcsr == 0x1f80U;
}

static void
run_preempt_window(void)
{
	volatile uint64_t i;

	for (i = 0; i < 30000000ULL; i++)
		__asm__ volatile ("pause" : : : "memory");
}

static int
child_run(void)
{
	const uint32_t child_mxcsr = 0x3f80U;
	unsigned int i;

	if (!xmm_matches(parent_pattern) || read_mxcsr() != 0x1f80U)
		return 10;
	set_xmm(child_pattern);
	__asm__ volatile ("ldmxcsr %0" : : "m" (child_mxcsr));
	run_preempt_window();
	if (!xmm_matches(child_pattern) || read_mxcsr() != child_mxcsr)
		return 12;
	for (i = 0; i < 40; i++) {
		if (u_sleep_ticks(1) != 0 || !xmm_matches(child_pattern) ||
		    read_mxcsr() != child_mxcsr)
			return 11;
	}
	return 0;
}

int
user_main(void)
{
	uint8_t child_status = 0xff;
	long child;
	unsigned int i;
	static const char passed[] = "OpenSillicon: Ring3 FPU/SSE context, fork, preemption, syscall, and exec tests passed\n";

	if (!read_initial_control_state() || !xmm_is_zero())
		return 1;
	set_xmm(parent_pattern);
	if (u_exec_path("/init/bin/fpu_exec_missing") >= 0 ||
	    !xmm_matches(parent_pattern) || read_mxcsr() != 0x1f80U)
		return 2;
	child = u_fork();
	if (child < 0)
		return 3;
	if (child == 0)
		u_exit((uint64_t)child_run());
	run_preempt_window();
	if (!xmm_matches(parent_pattern) || read_mxcsr() != 0x1f80U)
		return 13;
	for (i = 0; i < 40; i++) {
		if (u_sleep_ticks(1) != 0 || !xmm_matches(parent_pattern) ||
		    read_mxcsr() != 0x1f80U)
			return 4;
	}
	if (u_waitpid((uint64_t)child, &child_status) != child ||
	    child_status != 0 || !xmm_matches(parent_pattern) ||
	    read_mxcsr() != 0x1f80U)
		return 5;
	if (u_write(1, passed, sizeof(passed) - 1) != (long)(sizeof(passed) - 1))
		return 6;
	/* Successful exec must discard this non-default user FPU state. */
	{
		const uint32_t dirty_mxcsr = 0x5f80U;

		set_xmm(child_pattern);
		__asm__ volatile ("ldmxcsr %0" : : "m" (dirty_mxcsr));
	}
	if (u_exec_path("/init/bin/fpu_exec_child") >= 0)
		return 7;
	return 8;
}
