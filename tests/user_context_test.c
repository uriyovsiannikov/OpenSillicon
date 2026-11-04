#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "arch/x86_64/user.h"
#include "kernel/smp.h"

static struct smp_cpu cpus[SMP_MAX_CPUS];
static _Thread_local struct smp_cpu *current;
static pthread_barrier_t barrier;
extern uint64_t user_context_roundtrip(uint64_t status);

struct smp_cpu *smp_current_cpu(void) { return current; }
void klog(const char *message) { fputs(message, stderr); }
_Noreturn void kernel_halt(void) { abort(); }

static void *
worker(void *argument)
{
	uintptr_t id = (uintptr_t)argument;
	current = &cpus[id];
	for (uint64_t iteration = 1; iteration <= 1000; iteration++) {
		uint64_t stack = (id + 1) * 4096 + iteration;
		arch_user_context_save(stack);
		int result = pthread_barrier_wait(&barrier);
		assert(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
		struct user_kernel_return context = arch_user_context_return(id);
		assert(context.stack_pointer == stack && context.status == id);
		assert(current->user_kernel_stack == 0);
		assert(user_context_roundtrip(UINT64_MAX - iteration - id) ==
		    UINT64_MAX - iteration - id);
	}
	return NULL;
}

int
main(void)
{
	pthread_t threads[SMP_MAX_CPUS];
	/* The no-SMP path uses its own slot and returns through real assembly. */
	assert(user_context_roundtrip(0) == 0);
	assert(user_context_roundtrip(UINT64_MAX) == UINT64_MAX);
	assert(pthread_barrier_init(&barrier, NULL, SMP_MAX_CPUS) == 0);
	for (uintptr_t i = 0; i < SMP_MAX_CPUS; i++)
		assert(pthread_create(&threads[i], NULL, worker, (void *)i) == 0);
	for (size_t i = 0; i < SMP_MAX_CPUS; i++)
		assert(pthread_join(threads[i], NULL) == 0);
	assert(pthread_barrier_destroy(&barrier) == 0);
	puts("CPU-local user context: concurrent entries and assembly returns passed");
	return 0;
}
