#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "kernel/smp.h"

bool
smp_topology_build(const struct limine_mp_response *response,
	struct smp_cpu *cpus, size_t capacity, size_t *cpu_count)
{
	size_t count;
	size_t bsp_index = SMP_MAX_CPUS;
	size_t output_index = 0;

	if (response == NULL || cpus == NULL || cpu_count == NULL ||
	    response->cpu_count == 0 || response->cpu_count > SMP_MAX_CPUS ||
	    response->cpu_count > capacity || response->cpus == NULL ||
	    (response->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC) != 0)
		return false;
	count = (size_t)response->cpu_count;

	/* Validate every Limine record before touching the output topology. */
	for (size_t i = 0; i < count; i++) {
		struct limine_mp_info *info = response->cpus[i];

		if (info == NULL || info->lapic_id > UINT8_MAX)
			return false;
		for (size_t j = 0; j < i; j++) {
			if (response->cpus[j]->lapic_id == info->lapic_id ||
			    response->cpus[j]->processor_id == info->processor_id)
				return false;
		}
		if (info->lapic_id == response->bsp_lapic_id) {
			if (bsp_index != SMP_MAX_CPUS)
				return false;
			bsp_index = i;
		}
	}
	if (bsp_index == SMP_MAX_CPUS)
		return false;

	/* Logical zero is BSP; all hardware IDs stay independent of that index. */
	for (size_t pass = 0; pass < 2; pass++) {
		for (size_t i = 0; i < count; i++) {
			struct limine_mp_info *info = response->cpus[i];
			bool is_bsp = i == bsp_index;

			if ((pass == 0) != is_bsp)
				continue;
			cpus[output_index].self = &cpus[output_index];
			cpus[output_index].logical_id = (uint32_t)output_index;
			cpus[output_index].processor_id = info->processor_id;
			cpus[output_index].apic_id = info->lapic_id;
			cpus[output_index].bsp = is_bsp;
			cpus[output_index].state = SMP_CPU_DISCOVERED;
			cpus[output_index].stage = SMP_CPU_STAGE_DISCOVERED;
			cpus[output_index].tlb_request = 0;
			cpus[output_index].tlb_acknowledged = 0;
			cpus[output_index].current_process = NULL;
			cpus[output_index].scheduler_context = NULL;
			cpus[output_index].active_address_space = NULL;
			output_index++;
		}
	}
	*cpu_count = count;
	return true;
}
