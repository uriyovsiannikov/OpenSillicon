#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/lapic.h"
#include "kernel/process.h"
#include "kernel/smp.h"
#include "kernel/timer.h"
#include "kernel/spinlock.h"

#define TIMER_HZ 100U
#define TIMER_VECTOR 32U
#define PIC_MASTER_DATA 0x21U
#define PIC_SLAVE_DATA 0xa1U
#define TIMER_CALLBACK_SLOTS 64U

struct timer_callback_slot {
	uint64_t handle;
	uint64_t deadline_ns;
	kernel_timer_callback callback;
	void *argument;
	bool active;
};

static uint64_t ticks;
static uint64_t tsc_frequency_hz;
static uint32_t ap_initial_count;
static bool initialized;
static struct spinlock callback_lock = SPINLOCK_INITIALIZER;
static struct timer_callback_slot callbacks[TIMER_CALLBACK_SLOTS];
static uint64_t next_callback_handle = 1;

static void
timer_callbacks_run(void)
{
	for (size_t ran = 0; ran < TIMER_CALLBACK_SLOTS; ran++) {
		struct timer_callback_slot due = {0};
		uint64_t now = timer_monotonic_ns();
		uint64_t flags = spinlock_lock_irqsave(&callback_lock);
		for (size_t i = 0; i < TIMER_CALLBACK_SLOTS; i++) {
			if (callbacks[i].active && callbacks[i].deadline_ns <= now) {
				due = callbacks[i];
				callbacks[i].active = false;
				break;
			}
		}
		spinlock_unlock_irqrestore(&callback_lock, flags);
		if (due.callback == NULL)
			return;
		due.callback(due.argument);
	}
}

static struct interrupt_frame *
timer_interrupt(struct interrupt_frame *frame)
{
	uint64_t current = __atomic_load_n(&ticks, __ATOMIC_RELAXED);

	if (current != UINT64_MAX)
		current = __atomic_add_fetch(&ticks, 1, __ATOMIC_RELAXED);
	lapic_eoi();
	timer_callbacks_run();
	process_timer_wakeup(current);
	return process_timer_preempt(frame);
}

bool
timer_callback_schedule(uint64_t deadline_ns, kernel_timer_callback callback,
	void *argument, uint64_t *handle)
{
	uint64_t flags;
	if (callback == NULL || handle == NULL ||
	    !__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		return false;
	flags = spinlock_lock_irqsave(&callback_lock);
	for (size_t i = 0; i < TIMER_CALLBACK_SLOTS; i++) {
		if (callbacks[i].active)
			continue;
		uint64_t next = next_callback_handle++;
		if (next == 0)
			next = next_callback_handle++;
		callbacks[i] = (struct timer_callback_slot){
			.handle = next,
			.deadline_ns = deadline_ns,
			.callback = callback,
			.argument = argument,
			.active = true
		};
		*handle = next;
		spinlock_unlock_irqrestore(&callback_lock, flags);
		return true;
	}
	spinlock_unlock_irqrestore(&callback_lock, flags);
	return false;
}

bool
timer_callback_cancel(uint64_t handle)
{
	uint64_t flags;
	if (handle == 0)
		return false;
	flags = spinlock_lock_irqsave(&callback_lock);
	for (size_t i = 0; i < TIMER_CALLBACK_SLOTS; i++) {
		if (!callbacks[i].active || callbacks[i].handle != handle)
			continue;
		callbacks[i].active = false;
		spinlock_unlock_irqrestore(&callback_lock, flags);
		return true;
	}
	spinlock_unlock_irqrestore(&callback_lock, flags);
	return false;
}

bool
timer_init(uint64_t tsc_hz)
{
	uint32_t initial_count;
	uint64_t flags;
	bool success = false;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	if (__atomic_load_n(&initialized, __ATOMIC_ACQUIRE))
		goto out;
	if (tsc_hz > UINT64_MAX / UINT64_C(1000000000))
		tsc_hz = 0;
	__atomic_store_n(&tsc_frequency_hz, tsc_hz, __ATOMIC_RELAXED);
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

uint64_t
timer_jiffies(void)
{
	struct smp_cpu *cpu = smp_current_cpu();

	if (cpu != NULL && !cpu->bsp)
		return __atomic_load_n(&cpu->idle_timer_ticks, __ATOMIC_ACQUIRE);
	return timer_ticks();
}

uint64_t
timer_monotonic_ns(void)
{
	uint64_t frequency = __atomic_load_n(&tsc_frequency_hz, __ATOMIC_ACQUIRE);
	uint64_t cycles;
	uint32_t low, high;

	if (frequency == 0)
		return timer_jiffies() * UINT64_C(10000000);
	__asm__ volatile ("lfence; rdtsc" : "=a" (low), "=d" (high) : : "memory");
	cycles = ((uint64_t)high << 32) | low;
	return (cycles / frequency) * UINT64_C(1000000000) +
	    ((cycles % frequency) * UINT64_C(1000000000)) / frequency;
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
