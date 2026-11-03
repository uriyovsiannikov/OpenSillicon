#include <stdint.h>
#include <stddef.h>

#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/scheduler.h"
#include "kernel/smp.h"
#include "kernel/vmm.h"

#define THREAD_STACK_SIZE 16384U

struct sched_process;

struct thread {
	uint64_t *saved_stack;
	void *stack;
	thread_entry entry;
	void *argument;
	struct thread *previous;
	struct thread *next;
	struct thread *dead_next;
	struct sched_process *process;
};

struct sched_process {
	uint64_t id;
	struct address_space *address_space;
	size_t thread_count;
};

struct scheduler_cpu {
	struct thread *run_queue;
	struct thread *current;
	struct thread *dead_threads;
};

static struct thread boot_thread;
static struct scheduler_cpu bootstrap_cpu;
static struct scheduler_cpu *fallback_cpu;
static struct sched_process kernel_process;
static bool initialized;

extern void context_switch(uint64_t **old_stack, uint64_t *new_stack);
static void thread_bootstrap(void);

static struct scheduler_cpu *
scheduler_cpu_current(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu == NULL ? fallback_cpu : cpu->scheduler_context;
}

static uint64_t
interrupt_state_disable(void)
{
	uint64_t flags;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
}

static void
interrupt_state_restore(uint64_t flags)
{
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
}

static void
reap_dead_threads(void)
{
	struct scheduler_cpu *cpu = scheduler_cpu_current();
	struct thread *thread;

	if (cpu == NULL)
		return;
	thread = cpu->dead_threads;

	cpu->dead_threads = NULL;
	while (thread != NULL) {
		struct thread *next = thread->dead_next;

		if (!kheap_free(thread->stack) || !kheap_free(thread)) {
			klog("OpenSillicon panic: failed to release thread resources\n");
			kernel_halt();
		}
		thread = next;
	}
}

bool
scheduler_init(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	if (initialized)
		return false;
	if (cpu != NULL && !cpu->bsp)
		return false;
	boot_thread = (struct thread){0};
	boot_thread.previous = &boot_thread;
	boot_thread.next = &boot_thread;
	boot_thread.process = &kernel_process;
	kernel_process = (struct sched_process){
		.id = 0,
		.address_space = vmm_kernel_address_space(),
		.thread_count = 1
	};
	if (kernel_process.address_space == NULL)
		return false;
	bootstrap_cpu = (struct scheduler_cpu){
		.run_queue = &boot_thread,
		.current = &boot_thread,
		.dead_threads = NULL
	};
	if (cpu == NULL)
		fallback_cpu = &bootstrap_cpu;
	else
		cpu->scheduler_context = &bootstrap_cpu;
	initialized = true;
	return true;
}

bool
thread_create(thread_entry entry, void *argument)
{
	struct thread *thread;
	struct scheduler_cpu *cpu = scheduler_cpu_current();
	uintptr_t stack_top;
	uint64_t *initial_stack;
	uint64_t flags;

	if (!initialized || cpu == NULL || entry == NULL)
		return false;
	thread = kheap_alloc(sizeof(*thread));
	if (thread == NULL)
		return false;
	thread->stack = kheap_alloc(THREAD_STACK_SIZE);
	if (thread->stack == NULL) {
		if (!kheap_free(thread)) {
			klog("OpenSillicon panic: failed to release thread control block\n");
			kernel_halt();
		}
		return false;
	}
	thread->entry = entry;
	thread->argument = argument;
	thread->dead_next = NULL;
	thread->process = &kernel_process;
	stack_top = ((uintptr_t)thread->stack + THREAD_STACK_SIZE) & ~(uintptr_t)15;
	stack_top -= sizeof(uint64_t);
	initial_stack = (uint64_t *)(stack_top - 7 * sizeof(uint64_t));
	initial_stack[0] = 0;
	initial_stack[1] = 0;
	initial_stack[2] = 0;
	initial_stack[3] = 0;
	initial_stack[4] = 0;
	initial_stack[5] = 0;
	initial_stack[6] = (uintptr_t)thread_bootstrap;
	thread->saved_stack = initial_stack;
	flags = interrupt_state_disable();
	thread->previous = cpu->run_queue->previous;
	thread->next = cpu->run_queue;
	cpu->run_queue->previous->next = thread;
	cpu->run_queue->previous = thread;
	kernel_process.thread_count++;
	interrupt_state_restore(flags);
	return true;
}

static void
thread_remove(struct scheduler_cpu *cpu, struct thread *thread)
{
	thread->previous->next = thread->next;
	thread->next->previous = thread->previous;
	if (cpu->run_queue == thread)
		cpu->run_queue = thread->next;
	if (thread->process == NULL || thread->process->thread_count <= 1) {
		klog("OpenSillicon panic: invalid process thread count\n");
		kernel_halt();
	}
	thread->process->thread_count--;
}

void
thread_yield(void)
{
	struct scheduler_cpu *cpu = scheduler_cpu_current();
	struct thread *previous;
	struct thread *next;
	uint64_t flags;

	if (!initialized || cpu == NULL)
		return;
	flags = interrupt_state_disable();
	if ((flags & (1ULL << 9)) == 0) {
		klog("OpenSillicon panic: thread_yield with interrupts disabled\n");
		kernel_halt();
	}
	reap_dead_threads();
	previous = cpu->current;
	next = previous->next;
	if (next == previous) {
		interrupt_state_restore(flags);
		return;
	}
	cpu->current = next;
	interrupt_state_restore(flags);
	context_switch(&previous->saved_stack, next->saved_stack);
}

static void
thread_exit(void)
{
	struct scheduler_cpu *cpu = scheduler_cpu_current();
	struct thread *finished;
	struct thread *next;
	uint64_t flags;

	if (cpu == NULL) {
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	}
	finished = cpu->current;
	next = finished->next;

	if (finished == &boot_thread) {
		klog("OpenSillicon panic: bootstrap thread exited\n");
		kernel_halt();
	}
	flags = interrupt_state_disable();
	thread_remove(cpu, finished);
	finished->dead_next = cpu->dead_threads;
	cpu->dead_threads = finished;
	cpu->current = next;
	interrupt_state_restore(flags);
	context_switch(&finished->saved_stack, next->saved_stack);
	for (;;)
		__asm__ volatile ("cli; hlt");
}

static void
thread_bootstrap(void)
{
	struct scheduler_cpu *cpu = scheduler_cpu_current();
	thread_entry entry;
	void *argument;

	if (cpu == NULL || cpu->current == NULL) {
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	}
	entry = cpu->current->entry;
	argument = cpu->current->argument;

	entry(argument);
	thread_exit();
}
