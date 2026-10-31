#include <stdint.h>

#include "arch/x86_64/fpu.h"
#include "kernel/log.h"

#define CPUID_FXSR (1U << 24)
#define CPUID_SSE (1U << 25)
#define CPUID_SSE2 (1U << 26)
#define CR0_MP (1ULL << 1)
#define CR0_EM (1ULL << 2)
#define CR0_TS (1ULL << 3)
#define CR0_NE (1ULL << 5)
#define CR4_OSFXSR (1ULL << 9)
#define CR4_OSXMMEXCPT (1ULL << 10)

static _Alignas(16) uint8_t initial_state[ARCH_FPU_STATE_SIZE];
static _Alignas(16) uint8_t kernel_state[ARCH_FPU_STATE_SIZE];
static bool initialized;

static void
fxsave(void *state)
{
	__asm__ volatile ("fxsave64 %0" : "=m" (*(uint8_t (*)[512])state) : : "memory");
}

static void
fxrstor(const void *state)
{
	__asm__ volatile ("fxrstor64 %0" : : "m" (*(const uint8_t (*)[512])state) : "memory");
}

bool
arch_fpu_init(void)
{
	uint32_t eax = 1;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint64_t cr0;
	uint64_t cr4;
	const uint32_t default_mxcsr = 0x1f80U;

	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx) : : "memory");
	(void)ebx;
	(void)ecx;
	if ((edx & (CPUID_FXSR | CPUID_SSE | CPUID_SSE2)) !=
	    (CPUID_FXSR | CPUID_SSE | CPUID_SSE2))
		return false;
	__asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));
	cr0 = (cr0 | CR0_MP | CR0_NE) & ~(CR0_EM | CR0_TS);
	__asm__ volatile ("mov %0, %%cr0" : : "r" (cr0) : "memory");
	__asm__ volatile ("mov %%cr4, %0" : "=r" (cr4));
	cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
	__asm__ volatile ("mov %0, %%cr4" : : "r" (cr4) : "memory");
	__asm__ volatile ("fninit; ldmxcsr %0;"
	    "pxor %%xmm0, %%xmm0; pxor %%xmm1, %%xmm1;"
	    "pxor %%xmm2, %%xmm2; pxor %%xmm3, %%xmm3;"
	    "pxor %%xmm4, %%xmm4; pxor %%xmm5, %%xmm5;"
	    "pxor %%xmm6, %%xmm6; pxor %%xmm7, %%xmm7;"
	    "pxor %%xmm8, %%xmm8; pxor %%xmm9, %%xmm9;"
	    "pxor %%xmm10, %%xmm10; pxor %%xmm11, %%xmm11;"
	    "pxor %%xmm12, %%xmm12; pxor %%xmm13, %%xmm13;"
	    "pxor %%xmm14, %%xmm14; pxor %%xmm15, %%xmm15"
	    : : "m" (default_mxcsr) : "memory");
	fxsave(initial_state);
	fxsave(kernel_state);
	initialized = true;
	klog("OpenSillicon: FXSAVE, SSE, and SSE2 enabled; AVX disabled\n");
	return true;
}

void
arch_fpu_save(void *state)
{
	if (!initialized || state == NULL || ((uintptr_t)state & 15U) != 0) {
		klog("OpenSillicon panic: invalid FXSAVE destination\n");
		for (;;)
			__asm__ volatile ("cli; hlt");
	}
	fxsave(state);
}

void
arch_fpu_restore(const void *state)
{
	if (!initialized || state == NULL || ((uintptr_t)state & 15U) != 0) {
		klog("OpenSillicon panic: invalid FXRSTOR source\n");
		for (;;)
			__asm__ volatile ("cli; hlt");
	}
	fxrstor(state);
}

void
arch_fpu_init_process_state(void *state)
{
	uint64_t *destination = state;
	const uint64_t *source = (const uint64_t *)initial_state;
	size_t i;

	if (!initialized || state == NULL || ((uintptr_t)state & 15U) != 0) {
		klog("OpenSillicon panic: invalid initial FPU state destination\n");
		for (;;)
			__asm__ volatile ("cli; hlt");
	}
	for (i = 0; i < ARCH_FPU_STATE_SIZE / sizeof(*destination); i++)
		destination[i] = source[i];
}

void
arch_fpu_restore_kernel_state(void)
{
	arch_fpu_restore(kernel_state);
}
