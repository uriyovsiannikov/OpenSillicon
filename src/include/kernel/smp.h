#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "limine.h"
struct process;
struct scheduler_cpu;
struct address_space;
#define SMP_MAX_CPUS 8U
#define SMP_STACK_SIZE 16384U
#define SMP_TLB_SHOOTDOWN_VECTOR 2U
#define SMP_AP_IDLE_TIMER_VECTOR 0x31U
#define SMP_WORK_IPI_VECTOR 0xf1U
typedef void (*smp_work_function)(void *argument);
enum smp_cpu_state { SMP_CPU_EMPTY, SMP_CPU_DISCOVERED, SMP_CPU_STARTING, SMP_CPU_ONLINE, SMP_CPU_FAILED };
enum smp_cpu_stage {
	SMP_CPU_STAGE_DISCOVERED,
	SMP_CPU_STAGE_AP_ENTERED,
	SMP_CPU_STAGE_TABLES_READY,
	SMP_CPU_STAGE_LOCAL_READY
};
struct smp_cpu {
	struct smp_cpu *self;
	uint32_t logical_id;
	uint32_t processor_id;
	uint32_t apic_id;
	bool bsp;
	volatile uint32_t state;
	volatile uint32_t stage;
	volatile uint64_t tlb_request;
	volatile uint64_t tlb_acknowledged;
	volatile uint64_t idle_timer_ticks;
	volatile uint64_t work_request;
	volatile uint64_t work_acknowledged;
	smp_work_function work_function;
	void *work_argument;
	struct process *current_process;
	uint64_t user_kernel_stack;
	struct scheduler_cpu *scheduler_context;
	struct address_space *active_address_space;
	uint64_t stack_top;
	uint64_t gdt[8];
	uint64_t tss[13];
};
bool smp_topology_build(const struct limine_mp_response *response,
	struct smp_cpu *cpus, size_t capacity, size_t *cpu_count);
bool smp_init(struct limine_mp_response *response, uint64_t tsc_frequency,
	uint32_t ap_idle_timer_count);
bool smp_tlb_shootdown_all(void);
/* Synchronously invalidates translations on all other online CPUs that have
 * the address space active. Caller invalidates its local translation. */
bool smp_tlb_shootdown_space(const struct address_space *space);
bool smp_verify_ap_idle_timers(void);
/* BSP-only synchronous broadcast to all online CPUs. The callback runs once
 * per CPU (APs concurrently), with interrupts enabled, and must partition
 * shared work by smp_current_cpu()->logical_id. It must not block, broadcast
 * recursively, or mutate address spaces. The argument remains owned by the
 * caller until return. */
bool smp_run_on_all_cpus(smp_work_function function, void *argument);
bool smp_verify_work_dispatch(void);
/* Wake an online AP after publishing work on its CPU-local queue. */
bool smp_kick_cpu(size_t logical_id);
void smp_ap_entry(struct limine_mp_info *info);
_Noreturn void smp_ap_online(struct smp_cpu *cpu);
size_t smp_discovered_count(void);
size_t smp_online_count(void);
const struct smp_cpu *smp_cpu_at(size_t index);
/* Returns NULL until this CPU's per-CPU state has reached ONLINE. */
struct smp_cpu *smp_current_cpu(void);
#endif
