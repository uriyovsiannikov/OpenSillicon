#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/rdseed.h"

struct fake {
	uint32_t max_leaf;
	uint32_t feature_ebx;
	bool instruction_available;
	uint64_t value;
	bool carry;
	unsigned int instructions;
	unsigned int leaf7_queries;
	unsigned int fail_count;
	unsigned int fail_from_call;
	bool callback_failure;
};

static bool
fake_cpuid(void *context, uint32_t leaf, uint32_t subleaf,
	struct kernel_rdseed_cpuid_result *result)
{
	struct fake *fake = context;
	assert(subleaf == 0);
	memset(result, 0, sizeof(*result));
	if (leaf == 0) {
		result->eax = fake->max_leaf;
		return true;
	}
	if (leaf == 7) {
		fake->leaf7_queries++;
		result->ebx = fake->feature_ebx;
		return true;
	}
	assert(!"unexpected CPUID leaf");
	return false;
}

static bool
fake_instruction(void *context, uint64_t *value, bool *carry)
{
	struct fake *fake = context;
	fake->instructions++;
	*value = fake->value;
	*carry = fake->carry;
	if (fake->fail_count != 0) {
		fake->fail_count--;
		*carry = false;
		return true;
	}
	if (fake->fail_from_call != 0 &&
	    fake->instructions >= fake->fail_from_call) {
		*carry = false;
		return true;
	}
	return !fake->callback_failure;
}

static void
setup(struct fake *fake)
{
	kernel_rdseed_test_reset();
	assert(kernel_rdseed_test_set_backend(fake_cpuid, fake_instruction, fake));
}

static void
test_cpuid_detection(void)
{
	struct fake fake = { .max_leaf = 6, .feature_ebx = 1U << 18 };
	setup(&fake);
	assert(!kernel_rdseed_init());
	assert(kernel_rdseed_status() == KERNEL_RDSEED_UNSUPPORTED);
	assert(fake.leaf7_queries == 0);
	assert(fake.instructions == 0);
	assert(!kernel_rdseed_init());
	assert(fake.leaf7_queries == 0);
	assert(fake.instructions == 0);

	fake = (struct fake){ .max_leaf = 7, .feature_ebx = 1U << 17 };
	setup(&fake);
	assert(!kernel_rdseed_init());
	assert(kernel_rdseed_status() == KERNEL_RDSEED_UNSUPPORTED);
	uint64_t rejected_word = 0xfeedface;
	assert(kernel_rdseed_word(&rejected_word) ==
	    KERNEL_RDSEED_UNSUPPORTED_RESULT);
	assert(rejected_word == 0xfeedface);
	assert(fake.instructions == 0);

	fake = (struct fake){ .max_leaf = 7,
	    .feature_ebx = (1U << 18) | (1U << 2),
	    .carry = true, .value = 1 };
	setup(&fake);
	assert(kernel_rdseed_init());
	assert(kernel_rdseed_init());
	assert(fake.leaf7_queries == 1);
	assert(kernel_rdseed_supported());
	assert(kernel_rdseed_status() == KERNEL_RDSEED_SUPPORTED);
	assert(fake.instructions == 0); /* init performs detection only */
}

static void
test_word_carry_and_zero(void)
{
	uint64_t word = UINT64_C(0xdeadbeefcafebabe);
	struct fake fake = { .max_leaf = 7, .feature_ebx = 1U << 18,
	    .carry = true, .value = UINT64_C(0x0102030405060708) };
	setup(&fake);
	assert(kernel_rdseed_word(&word) == KERNEL_RDSEED_NOT_READY);
	assert(word == UINT64_C(0xdeadbeefcafebabe));
	assert(kernel_rdseed_init());
	assert(kernel_rdseed_word(&word) == KERNEL_RDSEED_OK);
	assert(word == fake.value && fake.instructions == 1);

	fake.value = 0;
	assert(kernel_rdseed_word(&word) == KERNEL_RDSEED_OK);
	assert(word == 0); /* CF, not the destination value, determines success. */

	fake.value = UINT64_C(0x9988776655443322);
	fake.carry = false;
	word = UINT64_C(0x123456789abcdef0);
	unsigned int before = fake.instructions;
	assert(kernel_rdseed_word(&word) == KERNEL_RDSEED_TEMPORARY_FAILURE);
	assert(fake.instructions - before == KERNEL_RDSEED_RETRY_LIMIT);
	assert(word == UINT64_C(0x123456789abcdef0));
	assert(kernel_rdseed_status() == KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE);
	assert(kernel_rdseed_supported());

	fake.carry = true;
	fake.value = 0x55;
	fake.fail_count = 3;
	before = fake.instructions;
	assert(kernel_rdseed_word(&word) == KERNEL_RDSEED_OK);
	assert(fake.instructions - before == 4);
	assert(word == 0x55);

	fake.carry = true;
	fake.value = 0x44;
	assert(kernel_rdseed_word(&word) == KERNEL_RDSEED_OK);
	assert(word == 0x44);
	assert(kernel_rdseed_status() == KERNEL_RDSEED_AVAILABLE);
}

static void
test_requests_atomicity_and_bounds(void)
{
	struct fake fake = { .max_leaf = 7, .feature_ebx = 1U << 18,
	    .carry = true, .value = UINT64_C(0x0807060504030201) };
	uint8_t output[KERNEL_RDSEED_MAX_REQUEST];
	uint8_t snapshot[KERNEL_RDSEED_MAX_REQUEST];
	setup(&fake);
	memset(output, 0xa5, sizeof(output));
	memcpy(snapshot, output, sizeof(output));
	assert(kernel_rdseed_bytes(output, 1) == KERNEL_RDSEED_NOT_READY);
	assert(memcmp(output, snapshot, sizeof(output)) == 0);
	assert(kernel_rdseed_init());
	assert(kernel_rdseed_bytes(NULL, 0) == KERNEL_RDSEED_OK);
	assert(kernel_rdseed_word(NULL) == KERNEL_RDSEED_INVALID_ARGUMENT);
	assert(kernel_rdseed_bytes(NULL, 1) == KERNEL_RDSEED_INVALID_ARGUMENT);
	assert(kernel_rdseed_bytes(output,
	    KERNEL_RDSEED_MAX_REQUEST + 1U) == KERNEL_RDSEED_INVALID_ARGUMENT);
	assert(kernel_rdseed_bytes(output, 1) == KERNEL_RDSEED_OK);
	assert(output[0] == 1 && output[1] == 0xa5);
	assert(kernel_rdseed_bytes(output, sizeof(output)) == KERNEL_RDSEED_OK);
	for (size_t i = 0; i < 8; i++)
		assert(output[i] == (uint8_t)(i + 1));

	memset(output, 0x5a, sizeof(output));
	memcpy(snapshot, output, sizeof(output));
	fake.value = 0x1122334455667788;
	fake.fail_from_call = fake.instructions + 2;
	fake.carry = true;
	assert(kernel_rdseed_bytes(output, 16) == KERNEL_RDSEED_TEMPORARY_FAILURE);
	assert(memcmp(output, snapshot, sizeof(output)) == 0);
	assert(kernel_rdseed_status() == KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE);
	fake.fail_from_call = 0;
	assert(kernel_rdseed_bytes(output, 16) == KERNEL_RDSEED_OK);
	assert(kernel_rdseed_status() == KERNEL_RDSEED_AVAILABLE);

	kernel_rdseed_test_reset();
	assert(kernel_rdseed_word(&fake.value) == KERNEL_RDSEED_NOT_READY);
	assert(kernel_rdseed_bytes(output, 1) == KERNEL_RDSEED_NOT_READY);
}

int
main(void)
{
	test_cpuid_detection();
	test_word_carry_and_zero();
	test_requests_atomicity_and_bounds();
	kernel_rdseed_test_reset();
	puts("RDSEED provider CPUID, CF, retry, bounds, atomicity: PASS");
	return 0;
}
