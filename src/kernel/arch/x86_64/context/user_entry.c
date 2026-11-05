#include "arch/x86_64/user.h"
#include "kernel/log.h"
#include "kernel/smp.h"

static uint64_t fallback_kernel_stack;

static uint64_t *
user_kernel_stack_slot(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	return cpu == NULL ? &fallback_kernel_stack : &cpu->user_kernel_stack;
}

void
arch_user_context_save(uint64_t stack_pointer)
{
	uint64_t *slot = user_kernel_stack_slot();
	if (stack_pointer == 0 || *slot != 0) {
		klog("OpenSillicon panic: invalid or nested CPU-local user entry\n");
		kernel_halt();
	}
	*slot = stack_pointer;
}

struct user_kernel_return
arch_user_context_return(uint64_t status)
{
	uint64_t *slot = user_kernel_stack_slot();
	struct user_kernel_return result = {*slot, status};
	if (*slot == 0) {
		klog("OpenSillicon panic: user return without CPU-local entry\n");
		kernel_halt();
	}
	*slot = 0;
	return result;
}
