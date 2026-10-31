#include <stdint.h>

#include "arch/x86_64/lapic.h"
#include "kernel/vmm.h"

#define IA32_APIC_BASE 0x1bU
#define APIC_BASE_ENABLE (1ULL << 11)
#define APIC_BASE_X2 (1ULL << 10)
#define APIC_PHYSICAL_MASK 0x000ffffffffff000ULL
#define LAPIC_SVR 0x0f0U
#define LAPIC_ID 0x020U
#define LAPIC_LVT_TIMER 0x320U
#define LAPIC_LVT_THERMAL 0x330U
#define LAPIC_LVT_PERF 0x340U
#define LAPIC_LVT_LINT0 0x350U
#define LAPIC_LVT_LINT1 0x360U
#define LAPIC_LVT_ERROR 0x370U
#define LAPIC_LVT_MASKED (1U << 16)
#define LAPIC_TIMER_DIVIDE 0x3e0U
#define LAPIC_TIMER_INITIAL 0x380U
#define LAPIC_TIMER_CURRENT 0x390U
#define LAPIC_EOI 0x0b0U
#define LAPIC_TIMER_PERIODIC (1U << 17)
#define PIT_CHANNEL2 0x42U
#define PIT_COMMAND 0x43U
#define PIT_SPEAKER 0x61U
#define PIT_FREQUENCY 1193182U
#define PIT_POLL_LIMIT 10000000U

static volatile uint32_t *lapic_registers;

static inline void
outb(uint16_t port, uint8_t value)
{
	__asm__ volatile ("outb %0, %1" : : "a" (value), "Nd" (port));
}

static inline uint8_t
inb(uint16_t port)
{
	uint8_t value;

	__asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

static uint64_t
read_msr(uint32_t msr)
{
	uint32_t low;
	uint32_t high;

	__asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
	return ((uint64_t)high << 32) | low;
}

static void
write_msr(uint32_t msr, uint64_t value)
{
	__asm__ volatile ("wrmsr" : : "c" (msr), "a" ((uint32_t)value),
	    "d" ((uint32_t)(value >> 32)));
}

static uint32_t
lapic_read(uint32_t offset)
{
	return lapic_registers[offset / sizeof(uint32_t)];
}

static void
lapic_write(uint32_t offset, uint32_t value)
{
	lapic_registers[offset / sizeof(uint32_t)] = value;
}

bool
lapic_init(void)
{
	uint64_t apic_base;
	uint64_t physical;
	uint64_t virtual_address;
	uint32_t eax = 1;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;

	if (lapic_registers != NULL || vmm_kernel_address_space() == NULL)
		return false;
	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx));
	if ((edx & (1U << 9)) == 0)
		return false;
	apic_base = read_msr(IA32_APIC_BASE);
	if ((apic_base & APIC_BASE_X2) != 0)
		return false;
	physical = apic_base & APIC_PHYSICAL_MASK;
	if (physical == 0)
		return false;
	apic_base = (apic_base & ~APIC_BASE_X2) | APIC_BASE_ENABLE;
	write_msr(IA32_APIC_BASE, apic_base);
	virtual_address = vmm_vmalloc_start();
	if (!vmm_map_page(vmm_kernel_address_space(), virtual_address, physical,
	    VMM_WRITE | VMM_UNCACHED))
		return false;
	lapic_registers = (volatile uint32_t *)(uintptr_t)virtual_address;
	(void)lapic_read(LAPIC_ID);
	lapic_write(LAPIC_LVT_TIMER,
	    lapic_read(LAPIC_LVT_TIMER) | LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_THERMAL,
	    lapic_read(LAPIC_LVT_THERMAL) | LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_PERF,
	    lapic_read(LAPIC_LVT_PERF) | LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_LINT0,
	    lapic_read(LAPIC_LVT_LINT0) | LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_LINT1,
	    lapic_read(LAPIC_LVT_LINT1) | LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_ERROR,
	    lapic_read(LAPIC_LVT_ERROR) | LAPIC_LVT_MASKED);
	lapic_write(LAPIC_SVR, 0x1ffU);
	return true;
}

uint32_t
lapic_id(void)
{
	return lapic_registers == NULL ? UINT32_MAX : lapic_read(LAPIC_ID) >> 24;
}

bool
lapic_timer_calibrate(uint32_t frequency, uint32_t *initial_count)
{
	uint32_t pit_count;
	uint32_t current;
	uint32_t elapsed;
	uint32_t i;
	uint8_t speaker_state;
	bool started = false;
	bool completed = false;

	if (lapic_registers == NULL || frequency == 0 || initial_count == NULL)
		return false;
	pit_count = PIT_FREQUENCY / frequency;
	if (pit_count == 0 || pit_count > UINT16_MAX)
		return false;
	lapic_write(LAPIC_TIMER_DIVIDE, 0x3U);
	lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED | 0x20U);
	lapic_write(LAPIC_TIMER_INITIAL, UINT32_MAX);
	speaker_state = inb(PIT_SPEAKER);
	outb(PIT_SPEAKER, speaker_state & (uint8_t)~0x03U);
	outb(PIT_COMMAND, 0xb0U);
	outb(PIT_CHANNEL2, (uint8_t)pit_count);
	outb(PIT_CHANNEL2, (uint8_t)(pit_count >> 8));
	outb(PIT_SPEAKER, (speaker_state & (uint8_t)~0x02U) | 0x01U);
	for (i = 0; i < PIT_POLL_LIMIT; i++) {
		if ((inb(PIT_SPEAKER) & 0x20U) == 0) {
			started = true;
			break;
		}
	}
	if (started) {
		for (i = 0; i < PIT_POLL_LIMIT; i++) {
			if ((inb(PIT_SPEAKER) & 0x20U) != 0) {
				completed = true;
				break;
			}
		}
	}
	current = lapic_read(LAPIC_TIMER_CURRENT);
	lapic_write(LAPIC_TIMER_INITIAL, 0);
	outb(PIT_SPEAKER, speaker_state);
	if (!completed)
		return false;
	elapsed = UINT32_MAX - current;
	if (elapsed == 0)
		return false;
	*initial_count = elapsed;
	return true;
}

bool
lapic_timer_start(uint8_t vector, uint32_t initial_count)
{
	if (lapic_registers == NULL || vector < 32 || vector == 255 ||
	    initial_count == 0)
		return false;
	lapic_write(LAPIC_TIMER_DIVIDE, 0x3U);
	lapic_write(LAPIC_LVT_TIMER, LAPIC_TIMER_PERIODIC | vector);
	lapic_write(LAPIC_TIMER_INITIAL, initial_count);
	return true;
}

void
lapic_eoi(void)
{
	if (lapic_registers != NULL)
		lapic_write(LAPIC_EOI, 0);
}
