#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/random.h"

struct test_source_state {
	uint64_t next;
	unsigned int calls;
	unsigned int fail_after;
	bool reenter;
	bool nested_result;
};

static bool
deterministic_word(void *context, uint64_t *word)
{
	struct test_source_state *state = context;
	state->calls++;
	if (state->reenter) {
		uint8_t byte;
		state->reenter = false;
		state->nested_result = kernel_random_bytes(&byte, sizeof(byte));
	}
	if (state->fail_after != 0 && state->calls >= state->fail_after)
		return false;
	*word = state->next++;
	return true;
}

static void
test_preinit_and_success(void)
{
	struct test_source_state source = {.next = UINT64_C(0x0102030405060708)};
	uint8_t bytes[10];
	unsigned int initialized_calls;
	assert(!kernel_random_available());
	for (size_t i = 0; i < sizeof(bytes); i++)
		bytes[i] = 0xa5;
	assert(!kernel_random_bytes(bytes, sizeof(bytes)));
	assert(bytes[0] == 0xa5 && bytes[sizeof(bytes) - 1] == 0xa5);
	assert(kernel_random_test_init_source(deterministic_word, &source));
	assert(kernel_random_available());
	initialized_calls = source.calls; /* one discarded initialization probe */
	assert(initialized_calls == 1);
	assert(kernel_random_init()); /* idempotent; does not reseed/reset */
	assert(source.calls == initialized_calls);
	source.reenter = true;
	assert(kernel_random_bytes(bytes, sizeof(bytes)));
	assert(bytes[0] == 0x09 && bytes[1] == 0x07 && bytes[7] == 0x01);
	assert(bytes[8] == 0x0a && bytes[9] == 0x07);
	assert(source.calls == initialized_calls + 2);
	assert(kernel_random_bytes(NULL, 0));
	assert(!kernel_random_bytes(bytes, KERNEL_RANDOM_MAX_REQUEST + 1U));
	assert(kernel_random_available());
	assert(!source.nested_result);
}

static void
test_failure_at_initialization(void)
{
	struct test_source_state source = {.fail_after = 1};
	uint8_t byte = 0x6c;
	assert(!kernel_random_test_init_source(deterministic_word, &source));
	assert(source.calls == 10);
	assert(!kernel_random_available());
	assert(!kernel_random_bytes(&byte, sizeof(byte)) && byte == 0x6c);
	assert(!kernel_random_init());
}

static void
test_failure_during_request(void)
{
	struct test_source_state source = {
		.next = UINT64_C(0x1122334455667788)
	};
	uint8_t bytes[16];
	assert(kernel_random_test_init_source(deterministic_word, &source));
	/* Probe + first request word succeed; the next word exhausts retries. */
	source.fail_after = source.calls + 2;
	for (size_t i = 0; i < sizeof(bytes); i++)
		bytes[i] = 0x3d;
	assert(!kernel_random_bytes(bytes, sizeof(bytes)));
	assert(!kernel_random_available());
	for (size_t i = 0; i < sizeof(bytes); i++)
		assert(bytes[i] == 0x3d); /* no partial output escaped */
	assert(!kernel_random_bytes(bytes, sizeof(bytes)));
	assert(!kernel_random_init());
}

int
main(void)
{
	kernel_random_test_reset();
	test_preinit_and_success();
	kernel_random_test_reset();
	test_failure_at_initialization();
	kernel_random_test_reset();
	test_failure_during_request();
	kernel_random_test_reset();
	puts("Kernel random API tests: unavailable, deterministic test source, bounds, reentrancy, and fail-closed behavior passed");
	return 0;
}
