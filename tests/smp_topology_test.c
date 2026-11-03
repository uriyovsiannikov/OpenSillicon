#include <stdio.h>

#include "kernel/smp.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "SMP topology test failed at line %d: %s\n", \
		    __LINE__, #condition); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	struct limine_mp_info bsp = {
		.processor_id = 100,
		.lapic_id = 42
	};
	struct limine_mp_info ap0 = {
		.processor_id = 17,
		.lapic_id = 201
	};
	struct limine_mp_info ap1 = {
		.processor_id = 8,
		.lapic_id = 7
	};
	struct limine_mp_info *infos[] = { &ap0, &bsp, &ap1 };
	struct limine_mp_response response = {
		.bsp_lapic_id = 42,
		.cpu_count = 3,
		.cpus = infos
	};
	struct smp_cpu cpus[SMP_MAX_CPUS] = { 0 };
	size_t count = 99;

	CHECK(smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	CHECK(count == 3);
	CHECK(cpus[0].self == &cpus[0] && cpus[1].self == &cpus[1] &&
	    cpus[2].self == &cpus[2]);
	CHECK(cpus[0].logical_id == 0 && cpus[0].bsp &&
	    cpus[0].processor_id == 100 && cpus[0].apic_id == 42);
	CHECK(cpus[1].logical_id == 1 && !cpus[1].bsp &&
	    cpus[1].processor_id == 17 && cpus[1].apic_id == 201);
	CHECK(cpus[2].logical_id == 2 && !cpus[2].bsp &&
	    cpus[2].processor_id == 8 && cpus[2].apic_id == 7);
	CHECK(cpus[0].state == SMP_CPU_DISCOVERED &&
	    cpus[0].stage == SMP_CPU_STAGE_DISCOVERED);

	/* Rejected maps do not leak partially rewritten records or counts. */
	cpus[0].logical_id = UINT32_C(0xdeadbeef);
	count = 77;
	ap1.lapic_id = ap0.lapic_id;
	CHECK(!smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	CHECK(cpus[0].logical_id == UINT32_C(0xdeadbeef) && count == 77);
	ap1.lapic_id = 7;

	ap1.processor_id = ap0.processor_id;
	CHECK(!smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	ap1.processor_id = 8;

	response.bsp_lapic_id = 99;
	CHECK(!smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	response.bsp_lapic_id = 42;

	ap0.lapic_id = 256;
	CHECK(!smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	ap0.lapic_id = 201;

	response.flags = LIMINE_MP_RESPONSE_X86_64_X2APIC;
	CHECK(!smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	response.flags = 0;
	CHECK(!smp_topology_build(&response, cpus, 2, &count));

	infos[0] = NULL;
	CHECK(!smp_topology_build(&response, cpus, SMP_MAX_CPUS, &count));
	infos[0] = &ap0;

	struct limine_mp_info many[SMP_MAX_CPUS + 1] = { 0 };
	struct limine_mp_info *many_infos[SMP_MAX_CPUS + 1];
	for (size_t i = 0; i < SMP_MAX_CPUS + 1; i++) {
		many[i].processor_id = (uint32_t)i;
		many[i].lapic_id = (uint32_t)i;
		many_infos[i] = &many[i];
	}
	struct limine_mp_response oversized = {
		.bsp_lapic_id = 0,
		.cpu_count = SMP_MAX_CPUS + 1,
		.cpus = many_infos
	};
	CHECK(!smp_topology_build(&oversized, cpus, SMP_MAX_CPUS, &count));

	puts("SMP topology tests passed (sparse IDs, BSP remap, invalid maps)");
	return 0;
}
