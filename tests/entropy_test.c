#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/entropy.h"
#include "kernel/random.h"
#include "kernel/rdseed.h"
#include "kernel/virtio_rng.h"

static enum kernel_random_source_status random_state =
	KERNEL_RANDOM_NOT_INITIALIZED;
static bool random_retry_succeeds;
static bool virtio_available;
static enum kernel_rdseed_status rdseed_state =
	KERNEL_RDSEED_NOT_INITIALIZED;
static enum kernel_rdseed_result rdseed_read_result = KERNEL_RDSEED_OK;
static uint8_t rdseed_byte = 0xc0;
static unsigned int rdseed_reads;

enum kernel_random_source_status
kernel_random_status(void)
{
	return random_state;
}

bool
kernel_random_retry(void)
{
	if (random_retry_succeeds) {
		random_state = KERNEL_RANDOM_AVAILABLE;
		return true;
	}
	return false;
}

bool
kernel_random_bytes(void *output, size_t length)
{
	(void)output;
	(void)length;
	assert(!"test override should intercept RDRAND reads");
	return false;
}

enum kernel_rdseed_status
kernel_rdseed_status(void)
{
	return rdseed_state;
}

bool
kernel_rdseed_supported(void)
{
	return rdseed_state == KERNEL_RDSEED_AVAILABLE ||
	    rdseed_state == KERNEL_RDSEED_SUPPORTED ||
	    rdseed_state == KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE;
}

enum kernel_rdseed_result
kernel_rdseed_bytes(void *output, size_t length)
{
	rdseed_reads++;
	if (rdseed_read_result != KERNEL_RDSEED_OK)
		return rdseed_read_result;
	for (size_t i = 0; i < length; i++)
		((uint8_t *)output)[i] = (uint8_t)(rdseed_byte + i);
	return KERNEL_RDSEED_OK;
}

bool
virtio_rng_available(void)
{
	return virtio_available;
}

enum virtio_rng_result
virtio_rng_read(void *output, size_t capacity, size_t *received)
{
	(void)output;
	(void)capacity;
	if (received != NULL)
		*received = 0;
	assert(!"test override should intercept VirtIO reads");
	return VIRTIO_RNG_UNAVAILABLE;
}

struct fake_source {
	enum kernel_entropy_result result;
	uint8_t first_byte;
	size_t result_length;
	unsigned int calls;
	bool write_before_failure;
};

static enum kernel_entropy_result
fake_read(void *context, uint8_t *output, size_t capacity,
	size_t *bytes_written)
{
	struct fake_source *source = context;
	source->calls++;
	*bytes_written = source->result_length;
	if (source->result == KERNEL_ENTROPY_OK || source->write_before_failure) {
		size_t count = source->result_length < capacity ?
		    source->result_length : capacity;
		for (size_t i = 0; i < count; i++)
			output[i] = (uint8_t)(source->first_byte + i);
	}
	return source->result;
}

static bool
set_fake(enum kernel_entropy_source id,
	enum kernel_entropy_source_status status, struct fake_source *fake)
{
	return kernel_entropy_test_set_source(id, status,
	    status == KERNEL_ENTROPY_CANDIDATE_AVAILABLE ? fake_read : NULL,
	    fake);
}

static void
test_initialization_and_unavailable(void)
{
	uint64_t output_values[BOOT_ENTROPY_MAX_VALUES] = {0};
	struct kernel_entropy_candidate result;
	uint8_t output[4] = {0xa5, 0xa5, 0xa5, 0xa5};

	kernel_entropy_test_reset();
	rdseed_state = KERNEL_RDSEED_UNSUPPORTED;
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_NOT_INITIALIZED);
	assert(!kernel_entropy_cryptographic_ready());
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    sizeof(output), &result) == KERNEL_ENTROPY_MANAGER_NOT_INITIALIZED);
	assert(output[0] == 0xa5 && result.bytes_written == 0);
	random_state = KERNEL_RANDOM_UNSUPPORTED;
	assert(kernel_entropy_init(BOOT_ENTROPY_MISSING, NULL, 0));
	assert(!kernel_entropy_init(BOOT_ENTROPY_MISSING, NULL, 0));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_LIMINE) ==
	    KERNEL_ENTROPY_UNAVAILABLE);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_UNSUPPORTED);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_NOT_INITIALIZED);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_UNSUPPORTED);
	assert(kernel_entropy_source_state((enum kernel_entropy_source)99) ==
	    KERNEL_ENTROPY_INVALID_SOURCE);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_LIMINE, output,
	    sizeof(output), &result) == KERNEL_ENTROPY_SOURCE_UNAVAILABLE);
	assert(memcmp(output, "\xa5\xa5\xa5\xa5", sizeof(output)) == 0);
	assert(kernel_entropy_virtio_probe_complete(false));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_DEVICE_ABSENT);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &result) ==
	    KERNEL_ENTROPY_SOURCE_UNAVAILABLE);
	assert(output_values[0] == 0);
}

static void
test_limine_provenance_and_exhaustion(void)
{
	uint64_t words[BOOT_ENTROPY_MAX_VALUES] = {
		0x0807060504030201ULL, 0x100f0e0d0c0b0a09ULL,
		0x1817161514131211ULL, 0x201f1e1d1c1b1a19ULL
	};
	uint8_t first[5] = {0};
	uint8_t tail[32] = {0};
	struct kernel_entropy_candidate candidate;

	kernel_entropy_test_reset();
	random_state = KERNEL_RANDOM_NOT_INITIALIZED;
	assert(kernel_entropy_init(BOOT_ENTROPY_AVAILABLE_UNTRUSTED, words,
	    BOOT_ENTROPY_MAX_VALUES));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_LIMINE) ==
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_LIMINE, first,
	    sizeof(first), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.source == KERNEL_ENTROPY_SOURCE_LIMINE);
	assert(candidate.trust == KERNEL_ENTROPY_UNTRUSTED_CANDIDATE);
	assert(candidate.bytes_written == sizeof(first));
	assert(memcmp(first, words, sizeof(first)) == 0);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_LIMINE, tail,
	    sizeof(tail), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.bytes_written == sizeof(tail) - sizeof(first));
	assert(memcmp(tail, (const uint8_t *)words + sizeof(first),
	    candidate.bytes_written) == 0);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_LIMINE) ==
	    KERNEL_ENTROPY_EXHAUSTED);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_LIMINE, tail,
	    sizeof(tail), &candidate) == KERNEL_ENTROPY_SOURCE_UNAVAILABLE);
	assert(!kernel_entropy_cryptographic_ready());
	kernel_entropy_test_reset();
	assert(kernel_entropy_init(BOOT_ENTROPY_PARTIAL_UNTRUSTED, words, 1));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_LIMINE, tail,
	    sizeof(tail), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.bytes_written == sizeof(uint64_t));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_LIMINE) ==
	    KERNEL_ENTROPY_EXHAUSTED);
}

static void
test_independent_source_reads_and_bounds(void)
{
	struct fake_source rdrand = { .result = KERNEL_ENTROPY_OK,
	    .first_byte = 0x31, .result_length = 4 };
	struct fake_source virtio = { .result = KERNEL_ENTROPY_OK,
	    .first_byte = 0xa1, .result_length = 3 };
	uint8_t output[8];
	struct kernel_entropy_candidate candidate;

	kernel_entropy_test_reset();
	random_state = KERNEL_RANDOM_PERMANENTLY_FAILED;
	assert(kernel_entropy_init(BOOT_ENTROPY_EMPTY, NULL, 0));
	assert(set_fake(KERNEL_ENTROPY_SOURCE_RDRAND,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &rdrand));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    sizeof(output), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.source == KERNEL_ENTROPY_SOURCE_RDRAND &&
	    candidate.bytes_written == 4 && candidate.trust ==
	    KERNEL_ENTROPY_UNTRUSTED_CANDIDATE);
	assert(output[0] == 0x31 && output[3] == 0x34 && rdrand.calls == 1);
	memset(output, 0x5c, sizeof(output));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_MANAGER_NOT_INITIALIZED);
	assert(output[0] == 0x5c && rdrand.calls == 1);
	virtio_available = true;
	assert(kernel_entropy_virtio_probe_complete(true));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_SUPPORTED);
	assert(set_fake(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &virtio));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.source == KERNEL_ENTROPY_SOURCE_VIRTIO_RNG &&
	    candidate.bytes_written == 3);
	assert(output[0] == 0xa1 && output[2] == 0xa3 && output[3] == 0x5c);
	assert(virtio.calls == 1 && rdrand.calls == 1);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, NULL,
	    1, &candidate) == KERNEL_ENTROPY_INVALID_ARGUMENT);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    KERNEL_ENTROPY_MAX_REQUEST + 1, &candidate) ==
	    KERNEL_ENTROPY_INVALID_ARGUMENT);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, NULL,
	    0, &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.bytes_written == 0 && rdrand.calls == 1);
	assert(kernel_entropy_get_candidate((enum kernel_entropy_source)77,
	    output, 1, &candidate) == KERNEL_ENTROPY_INVALID_ARGUMENT);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    1, NULL) == KERNEL_ENTROPY_INVALID_ARGUMENT);
}

static void
test_failure_latching_and_atomic_output(void)
{
	struct fake_source temporary = { .result =
	    KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE, .first_byte = 0x11,
	    .result_length = 2, .write_before_failure = true };
	struct fake_source permanent = { .result =
	    KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE, .first_byte = 0x22,
	    .result_length = 2, .write_before_failure = true };
	struct fake_source malformed = { .result = KERNEL_ENTROPY_OK,
	    .first_byte = 0x33, .result_length = 9 };
	struct fake_source unavailable = { .result =
	    KERNEL_ENTROPY_SOURCE_UNAVAILABLE };
	struct fake_source invalid_result = { .result =
	    (enum kernel_entropy_result)99 };
	uint8_t output[8];
	struct kernel_entropy_candidate candidate;

	kernel_entropy_test_reset();
	random_state = KERNEL_RANDOM_NOT_INITIALIZED;
	assert(kernel_entropy_init(BOOT_ENTROPY_MISSING, NULL, 0));
	memset(output, 0x77, sizeof(output));
	assert(set_fake(KERNEL_ENTROPY_SOURCE_RDRAND,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &temporary));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    sizeof(output), &candidate) == KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE);
	assert(output[0] == 0x77 && output[1] == 0x77);
	assert(candidate.bytes_written == 0 && temporary.calls == 1);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_TEMPORARILY_FAILED);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE);
	assert(temporary.calls == 1);
	temporary.result = KERNEL_ENTROPY_OK;
	assert(kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_RDRAND));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    sizeof(output), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.bytes_written == 2 && output[0] == 0x11);
	memset(output, 0x77, sizeof(output));
	assert(set_fake(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &permanent));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE);
	assert(output[0] == 0x77 && permanent.calls == 1);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_PERMANENTLY_DISABLED);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE);
	assert(permanent.calls == 1);
	assert(!kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG));
	assert(set_fake(KERNEL_ENTROPY_SOURCE_RDRAND,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &malformed));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDRAND, output,
	    sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE);
	assert(output[0] == 0x77 && candidate.bytes_written == 0);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_PERMANENTLY_DISABLED);
	assert(set_fake(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &unavailable));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_UNAVAILABLE);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_UNAVAILABLE);
	assert(set_fake(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, &invalid_result));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE);
	assert(!kernel_entropy_cryptographic_ready());
}

static void
test_invalid_initialization_is_atomic(void)
{
	uint64_t words[BOOT_ENTROPY_MAX_VALUES] = {0};
	kernel_entropy_test_reset();
	assert(!kernel_entropy_init(BOOT_ENTROPY_AVAILABLE_UNTRUSTED, words, 1));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_LIMINE) ==
	    KERNEL_ENTROPY_NOT_INITIALIZED);
	assert(kernel_entropy_init(BOOT_ENTROPY_PARTIAL_UNTRUSTED, words, 1));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_LIMINE) ==
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE);
}

static void
test_rdseed_manager_integration(void)
{
	uint8_t output[8];
	struct kernel_entropy_candidate candidate;

	kernel_entropy_test_reset();
	random_state = KERNEL_RANDOM_AVAILABLE;
	rdseed_state = KERNEL_RDSEED_UNSUPPORTED;
	rdseed_read_result = KERNEL_RDSEED_OK;
	rdseed_reads = 0;
	assert(kernel_entropy_init(BOOT_ENTROPY_EMPTY, NULL, 0));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_UNSUPPORTED);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDSEED, output,
	    sizeof(output), &candidate) == KERNEL_ENTROPY_SOURCE_UNAVAILABLE);
	assert(rdseed_reads == 0);

	kernel_entropy_test_reset();
	rdseed_state = KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE;
	assert(kernel_entropy_init(BOOT_ENTROPY_EMPTY, NULL, 0));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_TEMPORARILY_FAILED);
	assert(kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_RDSEED));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_SUPPORTED);

	kernel_entropy_test_reset();
	rdseed_state = KERNEL_RDSEED_SUPPORTED;
	assert(kernel_entropy_init(BOOT_ENTROPY_EMPTY, NULL, 0));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_SUPPORTED);
	memset(output, 0x5a, sizeof(output));
	rdseed_read_result = KERNEL_RDSEED_TEMPORARY_FAILURE;
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDSEED, output,
	    sizeof(output), &candidate) == KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_TEMPORARILY_FAILED);
	assert(memcmp(output, "ZZZZZZZZ", sizeof(output)) == 0);
	assert(rdseed_reads == 1);
	/* An available RDRAND source is deliberately not a fallback. */
	assert(kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_RDSEED));
	rdseed_state = KERNEL_RDSEED_AVAILABLE;
	rdseed_read_result = KERNEL_RDSEED_OK;
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_RDSEED, output,
	    sizeof(output), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.source == KERNEL_ENTROPY_SOURCE_RDSEED);
	assert(candidate.trust == KERNEL_ENTROPY_UNTRUSTED_CANDIDATE);
	assert(candidate.bytes_written == sizeof(output));
	assert(output[0] == rdseed_byte && output[7] == (uint8_t)(rdseed_byte + 7));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDSEED) ==
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE);
	assert(rdseed_reads == 2);
	assert(!kernel_entropy_cryptographic_ready());
	kernel_entropy_test_reset();
	rdseed_state = KERNEL_RDSEED_NOT_INITIALIZED;
}

static void
test_rdrand_temporary_state_and_explicit_retry(void)
{
	struct fake_source fake = { .result = KERNEL_ENTROPY_OK,
	    .first_byte = 0x61, .result_length = 1 };
	kernel_entropy_test_reset();
	random_state = KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE;
	random_retry_succeeds = false;
	assert(kernel_entropy_init(BOOT_ENTROPY_EMPTY, NULL, 0));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_TEMPORARILY_FAILED);
	assert(!kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_RDRAND));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_TEMPORARILY_FAILED);
	/* Test backend models the provider becoming available only after the
	 * explicit retry request; this does not select RDSEED or VirtIO. */
	assert(kernel_entropy_test_set_source(KERNEL_ENTROPY_SOURCE_RDRAND,
	    KERNEL_ENTROPY_TEMPORARILY_FAILED, fake_read, &fake));
	assert(kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_RDRAND));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_RDRAND) ==
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE);
	assert(!kernel_entropy_cryptographic_ready());
	kernel_entropy_test_reset();
	random_retry_succeeds = false;
}

static void
test_virtio_busy_is_caller_retryable(void)
{
	struct fake_source virtio = { .result =
	    KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE, .first_byte = 0x91,
	    .result_length = 2 };
	uint8_t output[4];
	struct kernel_entropy_candidate candidate;
	kernel_entropy_test_reset();
	random_state = KERNEL_RANDOM_AVAILABLE;
	virtio_available = true;
	assert(kernel_entropy_init(BOOT_ENTROPY_EMPTY, NULL, 0));
	assert(kernel_entropy_virtio_probe_complete(true));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_SUPPORTED);
	assert(kernel_entropy_test_set_source(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE, fake_read, &virtio));
	memset(output, 0x4d, sizeof(output));
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE);
	assert(output[0] == 0x4d && virtio.calls == 1);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_TEMPORARILY_FAILED);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) ==
	    KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE);
	assert(virtio.calls == 1); /* no hidden retry/polling */
	virtio.result = KERNEL_ENTROPY_OK;
	assert(kernel_entropy_retry_source(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG));
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_SUPPORTED);
	assert(kernel_entropy_get_candidate(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	    output, sizeof(output), &candidate) == KERNEL_ENTROPY_OK);
	assert(candidate.source == KERNEL_ENTROPY_SOURCE_VIRTIO_RNG &&
	    candidate.bytes_written == 2 && output[0] == 0x91 && output[1] == 0x92);
	assert(kernel_entropy_source_state(KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ==
	    KERNEL_ENTROPY_CANDIDATE_AVAILABLE);
	assert(!kernel_entropy_cryptographic_ready());
	kernel_entropy_test_reset();
	virtio_available = false;
}

int
main(void)
{
	test_initialization_and_unavailable();
	test_limine_provenance_and_exhaustion();
	test_independent_source_reads_and_bounds();
	test_failure_latching_and_atomic_output();
	test_invalid_initialization_is_atomic();
	test_rdseed_manager_integration();
	test_rdrand_temporary_state_and_explicit_retry();
	test_virtio_busy_is_caller_retryable();
	kernel_entropy_test_reset();
	puts("Kernel entropy manager source/provenance tests: PASS");
	return 0;
}
