#define _POSIX_C_SOURCE 200809L

#include <cpuid.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define RDSEED_BIT (1U << 18)
#define RDRAND_BIT (1U << 30)
#define HYPERVISOR_BIT (1U << 31)
#define REQUESTS 100U
#define RETRY_LIMIT 10U

struct cpuid_values {
	uint32_t eax, ebx, ecx, edx;
};

static struct cpuid_values
cpuid_query(uint32_t leaf, uint32_t subleaf)
{
	struct cpuid_values values;
	__cpuid_count(leaf, subleaf, values.eax, values.ebx, values.ecx, values.edx);
	return values;
}

static void
wipe(void *buffer, size_t length)
{
	volatile unsigned char *bytes = buffer;
	while (length-- != 0)
		*bytes++ = 0;
}

/* Mirrors kernel/drivers/entropy/rdseed.c's 64-bit destination and adjacent CF capture. The
 * invalid destination is never copied to caller output when CF is clear. */
static bool
rdseed_instruction(uint64_t *output)
{
	uint64_t value;
	unsigned char carry;
	__asm__ volatile ("rdseed %0; setc %1" : "=&r" (value), "=qm" (carry)
	    : : "cc");
	if (carry != 0)
		*output = value;
	wipe(&value, sizeof(value));
	return carry != 0;
}

static void
print_cpu_details(uint32_t maximum_basic_leaf)
{
	struct cpuid_values leaf0 = cpuid_query(0, 0);
	struct cpuid_values leaf1 = cpuid_query(1, 0);
	uint32_t base_family = (leaf1.eax >> 8) & 0xfU;
	uint32_t family = base_family;
	uint32_t model = (leaf1.eax >> 4) & 0xfU;
	if (base_family == 0xfU)
		family += (leaf1.eax >> 20) & 0xffU;
	if (base_family == 6U || base_family == 0xfU)
		model |= ((leaf1.eax >> 16) & 0xfU) << 4;
	char vendor[13];
	memcpy(vendor, &leaf0.ebx, 4);
	memcpy(vendor + 4, &leaf0.edx, 4);
	memcpy(vendor + 8, &leaf0.ecx, 4);
	vendor[12] = '\0';
	printf("CPUID vendor=%s max_basic_leaf=%u family=%u model=%u stepping=%u\n",
	    vendor, maximum_basic_leaf, family, model, leaf1.eax & 0xfU);
	printf("CPUID RDSEED=%s RDRAND=%s hypervisor_present=%s\n",
	    maximum_basic_leaf >= 7U &&
	    (cpuid_query(7, 0).ebx & RDSEED_BIT) != 0 ? "yes" : "no",
	    (leaf1.ecx & RDRAND_BIT) != 0 ? "yes" : "no",
	    (leaf1.ecx & HYPERVISOR_BIT) != 0 ? "yes" : "no");
	if ((leaf1.ecx & HYPERVISOR_BIT) != 0) {
		struct cpuid_values hypervisor = cpuid_query(0x40000000U, 0);
		char name[13];
		memcpy(name, &hypervisor.ebx, 4);
		memcpy(name + 4, &hypervisor.ecx, 4);
		memcpy(name + 8, &hypervisor.edx, 4);
		name[12] = '\0';
		printf("CPUID hypervisor_vendor=%s\n", name);
	} else {
		puts("CPUID hypervisor_vendor=not-exposed");
	}
}

int
main(void)
{
	unsigned int maximum_basic_leaf = __get_cpuid_max(0, NULL);
	unsigned int rdseed_attempts = 0;
	unsigned int success_count = 0;
	unsigned int cf0_count = 0;
	unsigned int retry_exhaustions = 0;
	struct timespec start, finish;

	if (maximum_basic_leaf == 0) {
		fputs("CPUID unavailable; RDSEED not executed\n", stderr);
		return 2;
	}
	print_cpu_details(maximum_basic_leaf);
	if (maximum_basic_leaf < 7U ||
	    (cpuid_query(7, 0).ebx & RDSEED_BIT) == 0) {
		puts("RDSEED unsupported; no instruction executed");
		return 0;
	}
	puts("RDSEED feature detected; running exactly 100 bounded requests");
	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
		perror("clock_gettime");
		return 2;
	}
	for (unsigned int request = 0; request < REQUESTS; request++) {
		bool completed = false;
		for (unsigned int retry = 0; retry < RETRY_LIMIT; retry++) {
			uint64_t word = 0;
			rdseed_attempts++;
			if (rdseed_instruction(&word)) {
				success_count++;
				completed = true;
				wipe(&word, sizeof(word));
				break;
			}
			cf0_count++;
			wipe(&word, sizeof(word));
			__asm__ volatile ("pause" : : : "memory");
		}
		if (!completed)
			retry_exhaustions++;
	}
	if (clock_gettime(CLOCK_MONOTONIC, &finish) != 0) {
		perror("clock_gettime");
		return 2;
	}
	long elapsed_us = (finish.tv_sec - start.tv_sec) * 1000000L +
	    (finish.tv_nsec - start.tv_nsec) / 1000L;
	printf("requests=%u instruction_attempts=%u successful_results=%u "
	    "CF0_results=%u retry_exhaustions=%u completed_requests=%u "
	    "elapsed_us=%ld\n", REQUESTS, rdseed_attempts, success_count,
	    cf0_count, retry_exhaustions, success_count, elapsed_us);
	if (success_count == 0)
		puts("bounded_instruction_check=INCONCLUSIVE (no valid result obtained)");
	else if (retry_exhaustions != 0)
		puts("bounded_instruction_check=PASS_WITH_TEMPORARY_UNAVAILABLE (operational behavior only)");
	else if (success_count != 0)
		puts("bounded_instruction_check=PASS (operational behavior only)");
	return success_count != 0 ? 0 : 1;
}
