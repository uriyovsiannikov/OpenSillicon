#include <stdint.h>

#include "include/os.h"

static uint32_t
read_mxcsr(void)
{
	uint32_t value;

	__asm__ volatile ("stmxcsr %0" : "=m" (value));
	return value;
}

static int
xmm_is_zero(void)
{
	uint64_t value[2];

	__asm__ volatile ("movdqu %%xmm0, %0" : "=m" (value));
	return value[0] == 0 && value[1] == 0;
}

int
user_main(void)
{
	uint64_t xmm[2];
	static const char passed[] = "OpenSillicon: successful exec restored clean FPU state\n";

	__asm__ volatile ("movdqu %%xmm0, %0" : "=m" (xmm));
	if (!xmm_is_zero() || xmm[0] != 0 || xmm[1] != 0 ||
	    read_mxcsr() != 0x1f80U)
		return 9;
	if (u_write(1, passed, sizeof(passed) - 1) != (long)(sizeof(passed) - 1))
		return 10;
	return 0;
}
