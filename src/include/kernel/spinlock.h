#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>

struct spinlock {
	uint32_t held;
};

#define SPINLOCK_INITIALIZER { 0 }
#define SPINLOCK_IRQ_ENABLED (1ULL << 9)

/*
 * Disable local maskable interrupts before spinning so an interrupt handler
 * cannot deadlock on a lock held by the code it interrupted. NMI handlers must
 * not acquire these locks. Locks are non-recursive and use acquire/release
 * ordering for communication between CPUs.
 */
static inline uint64_t
spinlock_lock_irqsave(struct spinlock *lock)
{
	uint64_t flags = 0;

#ifndef KERNEL_SPINLOCK_HOST_TEST
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
#endif
	while (__atomic_exchange_n(&lock->held, 1, __ATOMIC_ACQUIRE) != 0) {
		while (__atomic_load_n(&lock->held, __ATOMIC_RELAXED) != 0)
			__asm__ volatile ("pause" : : : "memory");
	}
	return flags;
}

static inline void
spinlock_unlock_irqrestore(struct spinlock *lock, uint64_t flags)
{
	__atomic_store_n(&lock->held, 0, __ATOMIC_RELEASE);
#ifndef KERNEL_SPINLOCK_HOST_TEST
	if ((flags & SPINLOCK_IRQ_ENABLED) != 0)
		__asm__ volatile ("sti" : : : "memory");
#else
	(void)flags;
#endif
}

#endif
