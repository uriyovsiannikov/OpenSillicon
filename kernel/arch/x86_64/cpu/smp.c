#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/lapic.h"
#include "kernel/smp.h"
#include "kernel/spinlock.h"
#include "kernel/vmm.h"

#define IA32_GS_BASE 0xc0000101U
#define STARTUP_POLL_LIMIT 100000000U
#define CPUID_FEATURE_TSC (1U << 4)
#define GDT_TSS_SELECTOR 0x30U
#define GDT_BOOTSTRAP_CODE_SELECTOR 0x28U
#define CPUID_FEATURE_APIC (1U << 9)
#define IDT_ENTRIES 256U
#define IDT_INTERRUPT_GATE 0x8eU

struct descriptor_pointer {
	uint16_t limit;
	uint64_t base;
} __attribute__((packed));

struct idt_gate {
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type_attributes;
	uint16_t offset_middle;
	uint32_t offset_high;
	uint32_t reserved;
} __attribute__((packed));

struct tss64 {
	uint32_t reserved0;
	uint64_t rsp0;
	uint64_t rsp1;
	uint64_t rsp2;
	uint64_t reserved1;
	uint64_t ist[7];
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss64) == 104,
	"x86-64 TSS must match the architectural descriptor limit");
_Static_assert(sizeof(((struct smp_cpu *)0)->tss) == sizeof(struct tss64),
	"per-CPU TSS storage must have the architectural size");
_Static_assert(offsetof(struct smp_cpu, self) == 0,
	"GS-relative CPU lookup requires the self pointer at offset zero");
_Static_assert(offsetof(struct smp_cpu, tss) % sizeof(uint64_t) == 0,
	"per-CPU TSS storage must be naturally aligned");
_Static_assert(sizeof(struct idt_gate) == 16,
	"x86-64 IDT gates must be 16 bytes");

static struct smp_cpu cpus[SMP_MAX_CPUS];
static struct idt_gate ap_startup_idt[IDT_ENTRIES];
static struct idt_gate ap_idle_idt[IDT_ENTRIES];
struct descriptor_pointer smp_ap_emergency_idtr;
static struct descriptor_pointer smp_ap_idle_idtr;
static uint8_t cpu_stacks[SMP_MAX_CPUS][SMP_STACK_SIZE]
	__attribute__((aligned(16)));
static size_t discovered_count;
static size_t online_count;
static uint64_t startup_tsc_frequency;
static uint32_t ap_idle_timer_count;
static uint64_t tlb_generation;
static struct spinlock tlb_shootdown_lock = SPINLOCK_INITIALIZER;

extern void gdt_load(const struct descriptor_pointer *pointer);
extern void idt_load(void);
extern void smp_ap_idle(struct smp_cpu *cpu, uint64_t stack_top);
extern void smp_ap_entry_trampoline(struct limine_mp_info *info);
extern void smp_ap_exception_halt(void);
extern void smp_ap_tlb_shootdown_entry(void);
extern void smp_ap_idle_timer_entry(void);
extern void smp_ap_spurious_iret(void);

static bool
current_apic_id(uint32_t *apic_id)
{
	uint32_t eax = 1;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;

	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx));
	(void)ecx;
	if ((edx & CPUID_FEATURE_APIC) == 0)
		return false;
	*apic_id = ebx >> 24;
	return true;
}

static bool
current_tsc_available(void)
{
	uint32_t eax = 1;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;

	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx));
	(void)ebx;
	(void)ecx;
	return (edx & CPUID_FEATURE_TSC) != 0;
}

static uint64_t
read_tsc(void)
{
	uint32_t low;
	uint32_t high;

	__asm__ volatile ("lfence; rdtsc" : "=a" (low), "=d" (high) : :
	    "memory");
	return ((uint64_t)high << 32) | low;
}

static void
prepare_ap_startup_idt(void)
{
	uintptr_t handler = (uintptr_t)smp_ap_exception_halt;
	struct idt_gate gate = {
		.offset_low = handler & 0xffffU,
		.selector = GDT_BOOTSTRAP_CODE_SELECTOR,
		.ist = 0,
		.type_attributes = IDT_INTERRUPT_GATE,
		.offset_middle = (handler >> 16) & 0xffffU,
		.offset_high = handler >> 32,
		.reserved = 0
	};

	for (size_t i = 0; i < IDT_ENTRIES; i++) {
		ap_startup_idt[i] = gate;
		ap_idle_idt[i] = gate;
	}
	handler = (uintptr_t)smp_ap_tlb_shootdown_entry;
	ap_idle_idt[SMP_TLB_SHOOTDOWN_VECTOR] = (struct idt_gate){
		.offset_low = handler & 0xffffU,
		.selector = GDT_BOOTSTRAP_CODE_SELECTOR,
		.ist = 0,
		.type_attributes = IDT_INTERRUPT_GATE,
		.offset_middle = (handler >> 16) & 0xffffU,
		.offset_high = handler >> 32,
		.reserved = 0
	};
	handler = (uintptr_t)smp_ap_spurious_iret;
	ap_idle_idt[255] = (struct idt_gate){
		.offset_low = handler & 0xffffU,
		.selector = GDT_BOOTSTRAP_CODE_SELECTOR,
		.ist = 0,
		.type_attributes = IDT_INTERRUPT_GATE,
		.offset_middle = (handler >> 16) & 0xffffU,
		.offset_high = handler >> 32,
		.reserved = 0
	};
	handler = (uintptr_t)smp_ap_idle_timer_entry;
	ap_idle_idt[SMP_AP_IDLE_TIMER_VECTOR] = (struct idt_gate){
		.offset_low = handler & 0xffffU,
		.selector = GDT_BOOTSTRAP_CODE_SELECTOR,
		.ist = 0,
		.type_attributes = IDT_INTERRUPT_GATE,
		.offset_middle = (handler >> 16) & 0xffffU,
		.offset_high = handler >> 32,
		.reserved = 0
	};
	smp_ap_emergency_idtr.limit = sizeof(ap_startup_idt) - 1U;
	smp_ap_emergency_idtr.base = (uintptr_t)ap_startup_idt;
	smp_ap_idle_idtr.limit = sizeof(ap_idle_idt) - 1U;
	smp_ap_idle_idtr.base = (uintptr_t)ap_idle_idt;
}

static void
write_msr(uint32_t msr, uint64_t value)
{
	__asm__ volatile ("wrmsr" : : "c" (msr), "a" ((uint32_t)value),
	    "d" ((uint32_t)(value >> 32)) : "memory");
}

static struct smp_cpu *
cpu_from_gs_base(void)
{
	struct smp_cpu *cpu;
	uintptr_t base;

	if (discovered_count == 0)
		return NULL;
	__asm__ volatile ("movq %%gs:0, %0" : "=r" (cpu) : : "memory");
	base = (uintptr_t)cpu;
	if (base < (uintptr_t)cpus ||
	    base >= (uintptr_t)(cpus + discovered_count) ||
	    (base - (uintptr_t)cpus) % sizeof(cpus[0]) != 0 || cpu->self != cpu)
		return NULL;
	return cpu;
}

struct smp_cpu *
smp_current_cpu(void)
{
	struct smp_cpu *cpu = cpu_from_gs_base();

	if (cpu == NULL || __atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) !=
	    SMP_CPU_ONLINE)
		return NULL;
	return cpu;
}

static bool
initialize_cpu_tables(struct smp_cpu *cpu, bool load_kernel_idt)
{
	struct tss64 *tss = (struct tss64 *)cpu->tss;
	uint64_t tss_base = (uintptr_t)tss;
	uint32_t tss_limit = sizeof(*tss) - 1U;
	struct descriptor_pointer pointer = {
		.limit = sizeof(cpu->gdt) - 1U,
		.base = (uintptr_t)cpu->gdt
	};

	cpu->gdt[0] = 0;
	cpu->gdt[1] = UINT64_C(0x00af9a000000ffff);
	cpu->gdt[2] = UINT64_C(0x00cf92000000ffff);
	cpu->gdt[3] = UINT64_C(0x00cff2000000ffff);
	cpu->gdt[4] = UINT64_C(0x00affa000000ffff);
	tss->rsp0 = cpu->stack_top;
	tss->iomap_base = sizeof(*tss);
	/* Keep selector 0x28 executable across the Limine-to-kernel GDT switch. */
	cpu->gdt[5] = UINT64_C(0x00af9a000000ffff);
	cpu->gdt[6] = (tss_limit & 0xffffU) |
	    ((tss_base & 0xffffffU) << 16) | (UINT64_C(0x89) << 40) |
	    ((uint64_t)(tss_limit & 0xf0000U) << 32) |
	    ((tss_base & UINT64_C(0xff000000)) << 32);
	cpu->gdt[7] = tss_base >> 32;

	gdt_load(&pointer);
	if (load_kernel_idt) {
		idt_load();
	} else {
		__asm__ volatile ("lidt %0" : : "m" (smp_ap_idle_idtr) : "memory");
	}
	__asm__ volatile ("ltr %0" : : "r" ((uint16_t)GDT_TSS_SELECTOR));
	write_msr(IA32_GS_BASE, (uintptr_t)cpu);
#if defined(KERNEL_TEST_SMP_FAIL_INIT_LOGICAL_CPU)
	if (!cpu->bsp && cpu->logical_id == KERNEL_TEST_SMP_FAIL_INIT_LOGICAL_CPU)
		return false;
#endif
	return cpu_from_gs_base() == cpu;
}

bool
smp_init(struct limine_mp_response *response, uint64_t tsc_frequency,
    uint32_t idle_timer_count)
{
	size_t cpu_count;
	uint32_t local_apic_id;

	if (tsc_frequency == 0 || idle_timer_count == 0 ||
	    !current_tsc_available() ||
	    response == NULL || !current_apic_id(&local_apic_id) ||
	    local_apic_id != response->bsp_lapic_id)
		return false;
	if (!smp_topology_build(response, cpus, SMP_MAX_CPUS, &cpu_count))
		return false;
	startup_tsc_frequency = tsc_frequency;
	ap_idle_timer_count = idle_timer_count;

	for (size_t i = 0; i < cpu_count; i++)
		cpus[i].stack_top =
		    (uintptr_t)&cpu_stacks[i][SMP_STACK_SIZE];

	/* AP entry reads the completed topology after Limine's acquire handoff. */
	discovered_count = cpu_count;
	if (!initialize_cpu_tables(&cpus[0], true))
		return false;
	__atomic_store_n(&cpus[0].active_address_space,
	    vmm_active_address_space(), __ATOMIC_RELEASE);
	__atomic_store_n(&cpus[0].stage, SMP_CPU_STAGE_LOCAL_READY,
	    __ATOMIC_RELEASE);
	__atomic_store_n(&cpus[0].state, SMP_CPU_ONLINE, __ATOMIC_RELEASE);
	online_count = 1;
	prepare_ap_startup_idt();

	for (size_t i = 1; i < discovered_count; i++) {
		struct limine_mp_info *info = NULL;

		for (size_t j = 0; j < discovered_count; j++) {
			if (response->cpus[j]->lapic_id == cpus[i].apic_id) {
				info = response->cpus[j];
				break;
			}
		}
		if (info == NULL) {
			cpus[i].state = SMP_CPU_FAILED;
			continue;
		}
		info->extra_argument = (uintptr_t)&cpus[i];
		__atomic_store_n(&cpus[i].state, SMP_CPU_STARTING,
		    __ATOMIC_RELEASE);
#if defined(KERNEL_TEST_SMP_TIMEOUT_LOGICAL_CPU)
		if (i == KERNEL_TEST_SMP_TIMEOUT_LOGICAL_CPU)
			continue;
#endif
		__atomic_store_n(&info->goto_address, smp_ap_entry_trampoline,
		    __ATOMIC_RELEASE);
	}

	/* TSC deadline is the timeout; the poll limit also covers a stopped TSC. */
	uint64_t startup_start = read_tsc();
	for (uint32_t polls = 0; polls < STARTUP_POLL_LIMIT; polls++) {
		bool starting = false;

		for (size_t i = 1; i < discovered_count; i++) {
			if (__atomic_load_n(&cpus[i].state, __ATOMIC_ACQUIRE) ==
			    SMP_CPU_STARTING) {
				starting = true;
				break;
			}
		}
		if (!starting)
			break;
		if (read_tsc() - startup_start >= tsc_frequency)
			break;
		__asm__ volatile ("pause" : : : "memory");
	}
	for (size_t i = 1; i < discovered_count; i++) {
		uint32_t expected = SMP_CPU_STARTING;

		if (!__atomic_compare_exchange_n(&cpus[i].state, &expected,
		    SMP_CPU_FAILED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) &&
		    expected == SMP_CPU_ONLINE)
			online_count++;
	}
	return smp_current_cpu() == &cpus[0];
}

void
smp_ap_entry(struct limine_mp_info *info)
{
	struct smp_cpu *cpu;
	uintptr_t cpu_address;
	uintptr_t cpu_base = (uintptr_t)cpus;
	uintptr_t cpu_end = cpu_base + sizeof(cpus);
	uint32_t local_apic_id;

	__asm__ volatile ("cli; cld" : : : "memory");
	cpu = info == NULL ? NULL :
	    (struct smp_cpu *)(uintptr_t)info->extra_argument;
	cpu_address = (uintptr_t)cpu;
	if (cpu == NULL || cpu_address < cpu_base || cpu_address >= cpu_end ||
	    (cpu_address - cpu_base) % sizeof(cpus[0]) != 0 ||
	    (cpu_address - cpu_base) / sizeof(cpus[0]) >= discovered_count ||
	    cpu->bsp || cpu->apic_id != info->lapic_id ||
	    !current_apic_id(&local_apic_id) || local_apic_id != info->lapic_id) {
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	}
	__atomic_store_n(&cpu->stage, SMP_CPU_STAGE_AP_ENTERED, __ATOMIC_RELEASE);

	if (!initialize_cpu_tables(cpu, false)) {
		uint32_t expected = SMP_CPU_STARTING;
		(void)__atomic_compare_exchange_n(&cpu->state, &expected,
		    SMP_CPU_FAILED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	}
	if (!lapic_ap_init(cpu->apic_id)) {
		uint32_t expected = SMP_CPU_STARTING;
		(void)__atomic_compare_exchange_n(&cpu->state, &expected,
		    SMP_CPU_FAILED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	}
	if (!lapic_timer_start(SMP_AP_IDLE_TIMER_VECTOR, ap_idle_timer_count)) {
		uint32_t expected = SMP_CPU_STARTING;
		(void)__atomic_compare_exchange_n(&cpu->state, &expected,
		    SMP_CPU_FAILED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	}
	__atomic_store_n(&cpu->active_address_space,
	    vmm_kernel_address_space(), __ATOMIC_RELEASE);
#if defined(KERNEL_TEST_SMP_AP_EXCEPTION_LOGICAL_CPU)
	if (cpu->logical_id == KERNEL_TEST_SMP_AP_EXCEPTION_LOGICAL_CPU)
		__asm__ volatile ("ud2" : : : "memory");
#endif
	__atomic_store_n(&cpu->stage, SMP_CPU_STAGE_TABLES_READY,
	    __ATOMIC_RELEASE);
	smp_ap_idle(cpu, cpu->stack_top);
}

_Noreturn void
smp_ap_online(struct smp_cpu *cpu)
{
	uint32_t expected = SMP_CPU_STARTING;
	__atomic_store_n(&cpu->stage, SMP_CPU_STAGE_LOCAL_READY, __ATOMIC_RELEASE);
	(void)__atomic_compare_exchange_n(&cpu->state, &expected, SMP_CPU_ONLINE,
	    false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
	for (;;)
		__asm__ volatile ("sti; hlt" : : : "memory");
}

void
smp_ap_tlb_shootdown_handler(void)
{
	struct smp_cpu *cpu = cpu_from_gs_base();
	uint64_t cr3;
	uint64_t generation;

	if (cpu == NULL || __atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE) !=
	    SMP_CPU_ONLINE)
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	__asm__ volatile ("mov %%cr3, %0" : "=r" (cr3));
	__asm__ volatile ("mov %0, %%cr3" : : "r" (cr3) : "memory");
	lapic_eoi();
	generation = __atomic_load_n(&cpu->tlb_request, __ATOMIC_ACQUIRE);
	__atomic_store_n(&cpu->tlb_acknowledged, generation, __ATOMIC_RELEASE);
}

void
smp_ap_idle_timer_handler(void)
{
	struct smp_cpu *cpu = cpu_from_gs_base();

	if (cpu == NULL || cpu->bsp ||
	    __atomic_load_n(&cpu->state, __ATOMIC_RELAXED) != SMP_CPU_ONLINE)
		for (;;)
			__asm__ volatile ("cli; hlt" : : : "memory");
	__atomic_add_fetch(&cpu->idle_timer_ticks, 1, __ATOMIC_RELAXED);
	lapic_eoi();
}

bool
smp_verify_ap_idle_timers(void)
{
	uint64_t start = read_tsc();

	for (uint32_t polls = 0; polls < STARTUP_POLL_LIMIT; polls++) {
		bool pending = false;

		for (size_t i = 1; i < discovered_count; i++) {
			if (__atomic_load_n(&cpus[i].state, __ATOMIC_ACQUIRE) ==
			    SMP_CPU_ONLINE && __atomic_load_n(&cpus[i].idle_timer_ticks,
			    __ATOMIC_ACQUIRE) == 0) {
				pending = true;
				break;
			}
		}
		if (!pending)
			return true;
		if (startup_tsc_frequency != 0 && read_tsc() - start >=
		    startup_tsc_frequency)
			return false;
		__asm__ volatile ("pause" : : : "memory");
	}
	return false;
}

bool
smp_tlb_shootdown_all(void)
{
	uint64_t flags = spinlock_lock_irqsave(&tlb_shootdown_lock);
	struct smp_cpu *current = smp_current_cpu();
	uint64_t generation;
	uint64_t startup_start;
	bool result = false;

	if (discovered_count == 0) {
		result = true;
		goto out;
	}
	if (current != &cpus[0])
		goto out;
	generation = __atomic_add_fetch(&tlb_generation, 1, __ATOMIC_RELAXED);
	for (size_t i = 1; i < discovered_count; i++) {
		if (__atomic_load_n(&cpus[i].state, __ATOMIC_ACQUIRE) !=
		    SMP_CPU_ONLINE)
			continue;
		__atomic_store_n(&cpus[i].tlb_request, generation, __ATOMIC_RELEASE);
		if (!lapic_send_ipi(cpus[i].apic_id, SMP_TLB_SHOOTDOWN_VECTOR))
			goto out;
	}
	startup_start = read_tsc();
	for (uint32_t polls = 0; polls < STARTUP_POLL_LIMIT; polls++) {
		bool pending = false;
		for (size_t i = 1; i < discovered_count; i++) {
			if (__atomic_load_n(&cpus[i].state, __ATOMIC_ACQUIRE) ==
			    SMP_CPU_ONLINE && __atomic_load_n(
			    &cpus[i].tlb_acknowledged, __ATOMIC_ACQUIRE) != generation) {
				pending = true;
				break;
			}
		}
		if (!pending) {
			result = true;
			break;
		}
		if (startup_tsc_frequency != 0 && read_tsc() - startup_start >=
		    startup_tsc_frequency)
			break;
		__asm__ volatile ("pause" : : : "memory");
	}
out:
	spinlock_unlock_irqrestore(&tlb_shootdown_lock, flags);
	return result;
}

size_t
smp_discovered_count(void)
{
	return discovered_count;
}

size_t
smp_online_count(void)
{
	return online_count;
}

const struct smp_cpu *
smp_cpu_at(size_t index)
{
	return index < discovered_count ? &cpus[index] : NULL;
}
