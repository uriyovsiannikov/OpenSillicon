#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/spinlock.h"

#define THREAD_COUNT 4U
#define ITERATION_COUNT 100000U

static struct spinlock lock = SPINLOCK_INITIALIZER;
static uint64_t counter;

static void *
worker(void *argument)
{
	(void)argument;
	for (uint32_t i = 0; i < ITERATION_COUNT; i++) {
		uint64_t flags = spinlock_lock_irqsave(&lock);
		counter++;
		spinlock_unlock_irqrestore(&lock, flags);
	}
	return NULL;
}

int
main(void)
{
	pthread_t threads[THREAD_COUNT];
	for (size_t i = 0; i < THREAD_COUNT; i++)
		if (pthread_create(&threads[i], NULL, worker, NULL) != 0)
			return 1;
	for (size_t i = 0; i < THREAD_COUNT; i++)
		if (pthread_join(threads[i], NULL) != 0)
			return 1;
	if (counter != (uint64_t)THREAD_COUNT * ITERATION_COUNT) {
		fprintf(stderr, "spinlock lost updates: %llu\n",
		    (unsigned long long)counter);
		return 1;
	}
	puts("spinlock concurrency test passed");
	return 0;
}
