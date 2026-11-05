#include <stdint.h>
#include <stddef.h>

#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/scheduler.h"
#include "kernel/smp.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"
#ifdef KERNEL_TEST_SMP_SCHEDULER
#include "kernel/timer.h"
#endif

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
	uint32_t logical_id;
	struct thread *run_queue;
	struct thread *current;
	struct thread *dead_threads;
	struct spinlock lock;
};

static struct thread boot_threads[SMP_MAX_CPUS];
static struct scheduler_cpu scheduler_cpus[SMP_MAX_CPUS];
static struct sched_process kernel_processes[SMP_MAX_CPUS];
static struct scheduler_cpu *fallback_cpu;
static bool initialized;
static volatile uint32_t secondary_init_failed;
static volatile uint32_t secondary_started;

extern void context_switch(uint64_t **old_stack, uint64_t *new_stack);
static void thread_bootstrap(void);

static struct scheduler_cpu *
scheduler_cpu_current(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	return cpu == NULL ? fallback_cpu : __atomic_load_n(
	    &cpu->scheduler_context, __ATOMIC_ACQUIRE);
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

static bool
scheduler_cpu_init(struct smp_cpu *cpu)
{
	uint32_t logical_id = cpu == NULL ? 0 : cpu->logical_id;
	struct thread *boot;
	struct scheduler_cpu *scheduler;
	struct sched_process *process;

	if (logical_id >= SMP_MAX_CPUS)
		return false;
	boot = &boot_threads[logical_id];
	scheduler = &scheduler_cpus[logical_id];
	process = &kernel_processes[logical_id];
	*boot = (struct thread){0};
	boot->previous = boot;
	boot->next = boot;
	boot->process = process;
	*process = (struct sched_process){
		.id = logical_id,
		.address_space = vmm_kernel_address_space(),
		.thread_count = 1
	};
	if (process->address_space == NULL)
		return false;
	*scheduler = (struct scheduler_cpu){
		.logical_id = logical_id,
		.run_queue = boot,
		.current = boot,
		.dead_threads = NULL,
		.lock = SPINLOCK_INITIALIZER
	};
	if (cpu == NULL)
		fallback_cpu = scheduler;
	else
		__atomic_store_n(&cpu->scheduler_context, scheduler, __ATOMIC_RELEASE);
	return true;
}

static void
scheduler_attach_secondary(void *argument)
{
	struct smp_cpu *cpu = smp_current_cpu();

	(void)argument;
	if (cpu == NULL || cpu->bsp)
		return;
	if (!scheduler_cpu_init(cpu))
		__atomic_store_n(&secondary_init_failed, 1, __ATOMIC_RELEASE);
}

bool
scheduler_init(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ||
	    (cpu != NULL && !cpu->bsp))
		return false;
	if (!scheduler_cpu_init(cpu))
		return false;
	__atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
	return true;
}

bool
scheduler_start_secondary_cpus(void)
{
	uint32_t expected = 0;

	if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		return false;
	if (!__atomic_compare_exchange_n(&secondary_started, &expected, 1, false,
	    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	if (smp_current_cpu() == NULL || smp_online_count() <= 1)
		return true;
	if (!smp_run_on_all_cpus(scheduler_attach_secondary, NULL))
		return false;
	return __atomic_load_n(&secondary_init_failed, __ATOMIC_ACQUIRE) == 0;
}

void
scheduler_idle(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	uint64_t flags;

	if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) || cpu == NULL ||
	    cpu->bsp || scheduler_cpu_current() == NULL)
		return;
	__asm__ volatile ("pushfq; popq %0" : "=r" (flags));
	if ((flags & (1ULL << 9)) != 0)
		thread_yield();
}

bool
scheduler_has_work(void)
{
	struct scheduler_cpu *cpu;
	bool pending;
	uint64_t flags;
	if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		return false;
	cpu = scheduler_cpu_current();
	if (cpu == NULL)
		return false;
	flags = spinlock_lock_irqsave(&cpu->lock);
	pending = cpu->current->next != cpu->current || cpu->dead_threads != NULL;
	spinlock_unlock_irqrestore(&cpu->lock, flags);
	return pending;
}

bool
thread_create_on_cpu(size_t logical_id, thread_entry entry, void *argument)
{
	struct smp_cpu *current_cpu = smp_current_cpu();
	struct scheduler_cpu *scheduler;
	struct sched_process *process;
	struct thread *thread;
	uintptr_t stack_top;
	uint64_t *initial_stack;
	uint64_t flags;
	bool remote;
	bool target_is_smp;

	if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) || entry == NULL ||
	    logical_id >= SMP_MAX_CPUS)
		return false;
	if (logical_id == 0 && current_cpu != NULL && !current_cpu->bsp)
		return false;
	if (smp_discovered_count() == 0) {
		if (logical_id != 0)
			return false;
		target_is_smp = false;
		scheduler = fallback_cpu;
	} else {
		const struct smp_cpu *target = smp_cpu_at(logical_id);
		if (target == NULL || __atomic_load_n(&target->state,
		    __ATOMIC_ACQUIRE) != SMP_CPU_ONLINE)
			return false;
		target_is_smp = true;
		scheduler = __atomic_load_n(&target->scheduler_context,
		    __ATOMIC_ACQUIRE);
	}
	if (scheduler == NULL)
		return false;
	process = &kernel_processes[logical_id];
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
	thread->process = process;
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
	flags = spinlock_lock_irqsave(&scheduler->lock);
	thread->previous = scheduler->run_queue->previous;
	thread->next = scheduler->run_queue;
	scheduler->run_queue->previous->next = thread;
	scheduler->run_queue->previous = thread;
	process->thread_count++;
	spinlock_unlock_irqrestore(&scheduler->lock, flags);
	remote = current_cpu == NULL ? logical_id != 0 :
	    current_cpu->logical_id != logical_id;
	if (remote && target_is_smp && !smp_kick_cpu(logical_id)) {
		klog("OpenSillicon panic: failed to wake scheduled CPU\n");
		kernel_halt();
	}
	return true;
}

bool
thread_create(thread_entry entry, void *argument)
{
	struct smp_cpu *cpu = smp_current_cpu();

	if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ||
	    scheduler_cpu_current() == NULL)
		return false;
	return thread_create_on_cpu(cpu == NULL ? 0 : cpu->logical_id, entry,
	    argument);
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
	uint64_t lock_flags;

	if (!initialized || cpu == NULL)
		return;
	flags = interrupt_state_disable();
	if ((flags & (1ULL << 9)) == 0) {
		klog("OpenSillicon panic: thread_yield with interrupts disabled\n");
		kernel_halt();
	}
	reap_dead_threads();
	lock_flags = spinlock_lock_irqsave(&cpu->lock);
	previous = cpu->current;
	next = previous->next;
	if (next == previous) {
		spinlock_unlock_irqrestore(&cpu->lock, lock_flags);
		interrupt_state_restore(flags);
		return;
	}
	cpu->current = next;
	spinlock_unlock_irqrestore(&cpu->lock, lock_flags);
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
	flags = interrupt_state_disable();
	uint64_t lock_flags = spinlock_lock_irqsave(&cpu->lock);
	finished = cpu->current;
	next = finished->next;
	if (finished == &boot_threads[cpu->logical_id]) {
		klog("OpenSillicon panic: bootstrap thread exited\n");
		kernel_halt();
	}
	thread_remove(cpu, finished);
	finished->dead_next = cpu->dead_threads;
	cpu->dead_threads = finished;
	cpu->current = next;
	spinlock_unlock_irqrestore(&cpu->lock, lock_flags);
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

#ifdef KERNEL_TEST_SMP_SCHEDULER
struct scheduler_probe {
	size_t logical_id;
	struct scheduler_probe *child;
	volatile uint32_t steps;
	volatile uint32_t done;
	volatile uint32_t failed;
};

static struct scheduler_probe probes[SMP_MAX_CPUS][2];
static struct scheduler_probe child_probes[SMP_MAX_CPUS][2];
static volatile uint32_t probe_reap_failed;

static void
scheduler_probe_entry(void *argument)
{
	struct scheduler_probe *probe = argument;
	volatile uint64_t stack_guard = UINT64_C(0x123456789abcdef0);
	volatile uint64_t *memory = kheap_alloc(8192);
	if (probe->child != NULL &&
	    !thread_create_on_cpu(probe->child->logical_id,
	    scheduler_probe_entry, probe->child))
		__atomic_store_n(&probe->failed, 1, __ATOMIC_RELEASE);

	if (memory == NULL) {
		__atomic_store_n(&probe->failed, 1, __ATOMIC_RELEASE);
	} else {
		memory[0] = probe->logical_id;
		memory[1023] = (uintptr_t)probe;
		for (unsigned int round = 0; round < 32; round++) {
			struct smp_cpu *cpu = smp_current_cpu();
			if ((cpu == NULL ? 0 : cpu->logical_id) != probe->logical_id ||
			    vmm_active_address_space() != vmm_kernel_address_space() ||
			    stack_guard != UINT64_C(0x123456789abcdef0) ||
			    memory[0] != probe->logical_id || memory[1023] != (uintptr_t)probe)
				__atomic_store_n(&probe->failed, 1, __ATOMIC_RELEASE);
			__atomic_add_fetch(&probe->steps, 1, __ATOMIC_RELEASE);
			thread_yield();
		}
		if (!kheap_free((void *)memory))
			__atomic_store_n(&probe->failed, 1, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&probe->done, 1, __ATOMIC_RELEASE);
}

static void
scheduler_probe_reap(void *argument)
{
	struct scheduler_cpu *cpu = scheduler_cpu_current();
	(void)argument;
	if (cpu == NULL) {
		__atomic_store_n(&probe_reap_failed, 1, __ATOMIC_RELEASE);
		return;
	}
	reap_dead_threads();
	uint64_t flags = spinlock_lock_irqsave(&cpu->lock);
	if (cpu->current != &boot_threads[cpu->logical_id] ||
	    cpu->run_queue != cpu->current || cpu->current->next != cpu->current ||
	    kernel_processes[cpu->logical_id].thread_count != 1 ||
	    cpu->dead_threads != NULL)
		__atomic_store_n(&probe_reap_failed, 1, __ATOMIC_RELEASE);
	spinlock_unlock_irqrestore(&cpu->lock, flags);
}

bool
scheduler_verify_cpu_queues(void)
{
	size_t count = smp_discovered_count();
	bool targets[SMP_MAX_CPUS] = {false};
	uint64_t start = timer_ticks();
	if (thread_create_on_cpu(SMP_MAX_CPUS, scheduler_probe_entry, NULL) ||
	    thread_create_on_cpu(0, NULL, NULL))
		return false;
	if (count == 0)
		count = 1;
	/* Initialize every argument before any AP can run its parent thread. */
	for (size_t i = 0; i < count; i++) {
		const struct smp_cpu *cpu = smp_cpu_at(i);
		targets[i] = cpu == NULL || __atomic_load_n(&cpu->state,
		    __ATOMIC_ACQUIRE) == SMP_CPU_ONLINE;
	}
	for (size_t i = 0; i < count; i++) {
		if (!targets[i])
			continue;
		size_t destination = i;
		/* APs can submit to other APs, while the BSP process scheduler
		 * still owns CPU 0. Skip failed CPUs and wrap within the AP set. */
		for (size_t step = 1; step <= count; step++) {
			size_t candidate = (i + step) % count;
			if (candidate != 0 && targets[candidate]) {
				destination = candidate;
				break;
			}
		}
		for (size_t j = 0; j < 2; j++) {
			child_probes[i][j] = (struct scheduler_probe){
				.logical_id = destination
			};
			probes[i][j] = (struct scheduler_probe){
				.logical_id = i, .child = &child_probes[i][j]
			};
		}
	}
	for (size_t i = 0; i < count; i++) {
		const struct smp_cpu *cpu = smp_cpu_at(i);
		if (cpu != NULL && __atomic_load_n(&cpu->state,
		    __ATOMIC_ACQUIRE) != SMP_CPU_ONLINE) {
			if (thread_create_on_cpu(i, scheduler_probe_entry, NULL))
				return false;
			continue;
		}
		targets[i] = true;
		for (size_t j = 0; j < 2; j++) {
			if (!thread_create_on_cpu(i, scheduler_probe_entry, &probes[i][j]))
				return false;
		}
	}
	for (;;) {
		bool pending = false;
		thread_yield();
		for (size_t i = 0; i < count; i++)
			for (size_t j = 0; targets[i] && j < 2; j++) {
				if (!__atomic_load_n(&probes[i][j].done, __ATOMIC_ACQUIRE))
					pending = true;
				if (!__atomic_load_n(&child_probes[i][j].done, __ATOMIC_ACQUIRE))
					pending = true;
				if (__atomic_load_n(&probes[i][j].failed, __ATOMIC_ACQUIRE))
					return false;
				if (__atomic_load_n(&child_probes[i][j].failed, __ATOMIC_ACQUIRE))
					return false;
			}
		if (!pending)
			break;
		if (timer_ticks() - start > 1000)
			return false;
		__asm__ volatile ("pause");
	}
	for (size_t i = 0; i < count; i++)
		for (size_t j = 0; targets[i] && j < 2; j++)
			if (__atomic_load_n(&probes[i][j].steps, __ATOMIC_ACQUIRE) != 32 ||
			    __atomic_load_n(&child_probes[i][j].steps, __ATOMIC_ACQUIRE) != 32)
				return false;
	if (smp_current_cpu() != NULL) {
		if (!smp_run_on_all_cpus(scheduler_probe_reap, NULL))
			return false;
	} else
		scheduler_probe_reap(NULL);
	if (__atomic_load_n(&probe_reap_failed, __ATOMIC_ACQUIRE) != 0)
		return false;
	for (size_t i = 0; i < count; i++) {
		if (!targets[i])
			continue;
		klog("OpenSillicon test: scheduler CPU ");
		klog_u64(i);
		klog(" threads 2 yields 64 reaped 2\n");
		klog("OpenSillicon test: scheduler submit CPU ");
		klog_u64(i);
		klog(" target ");
		klog_u64(child_probes[i][0].logical_id);
		klog(" children 2 yields 64 reaped 2\n");
	}
	return true;
}
#endif
