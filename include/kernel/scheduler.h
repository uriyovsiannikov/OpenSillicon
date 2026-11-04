#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>

typedef void (*thread_entry)(void *argument);

bool scheduler_init(void);
/* Attach an initialized CPU-local kernel-thread queue to every online AP. */
bool scheduler_start_secondary_cpus(void);
bool thread_create(thread_entry entry, void *argument);
/* Creates a pinned kernel thread. Remote queueing targets online APs; BSP
 * queueing is local and requires the caller to yield when appropriate. */
bool thread_create_on_cpu(size_t logical_id, thread_entry entry,
	void *argument);
/* Kernel threads are cooperative and yield explicitly or when they return.
 * AP entries must not change VMM mappings outside the kernel heap range. */
void thread_yield(void);
/* Called by the AP idle loop to run its next cooperative kernel thread. */
void scheduler_idle(void);
/* CPU-local idle-loop check, including deferred thread cleanup. */
bool scheduler_has_work(void);
#ifdef KERNEL_TEST_SMP_SCHEDULER
bool scheduler_verify_cpu_queues(void);
#endif

#endif
