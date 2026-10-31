#include <stdint.h>

#include "arch/x86_64/lapic.h"
#include "kernel/pmm.h"
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

static volatile uint32_t *lapic_registers;

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
