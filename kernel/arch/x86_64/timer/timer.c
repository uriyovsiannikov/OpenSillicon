#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/lapic.h"
#include "kernel/process.h"
#include "kernel/timer.h"

#define TIMER_HZ 100U
#define TIMER_VECTOR 32U
#define PIC_MASTER_DATA 0x21U
#define PIC_SLAVE_DATA 0xa1U

static uint64_t ticks;
static uint32_t ap_initial_count;
static bool initialized;

static struct interrupt_frame *
timer_interrupt(struct interrupt_frame *frame)
{
	uint64_t current = __atomic_load_n(&ticks, __ATOMIC_RELAXED);

	if (current != UINT64_MAX)
		current = __atomic_add_fetch(&ticks, 1, __ATOMIC_RELAXED);
	lapic_eoi();
	process_timer_wakeup(current);
	return process_timer_preempt(frame);
}

bool
timer_init(void)
{
	uint32_t initial_count;
	uint64_t flags;
	bool success = false;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		goto out;
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)0xff),
	    "Nd" ((uint16_t)PIC_MASTER_DATA));
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)0xff),
	    "Nd" ((uint16_t)PIC_SLAVE_DATA));
	if (!lapic_timer_calibrate(TIMER_HZ, &initial_count) ||
	    !interrupt_register_handler(TIMER_VECTOR, timer_interrupt))
		goto out;
	ap_initial_count = initial_count;
	__atomic_store_n(&ticks, 0, __ATOMIC_RELAXED);
	if (!lapic_timer_start(TIMER_VECTOR, initial_count))
		goto out;
	__atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
	success = true;
out:
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return success;
}

uint64_t
timer_ticks(void)
{
	return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ?
	    __atomic_load_n(&ticks, __ATOMIC_RELAXED) : 0;
}

uint32_t
timer_ap_initial_count(void)
{
	return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) ?
	    ap_initial_count : 0;
}

void
timer_wait_for_interrupt(void)
{
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		__asm__ volatile ("sti; hlt; cli" : : : "memory");
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
}
