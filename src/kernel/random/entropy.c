#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/entropy.h"
#include "kernel/random.h"
#include "kernel/rdseed.h"
#include "kernel/spinlock.h"
#include "kernel/virtio_rng.h"

#define LIMINE_BYTES (BOOT_ENTROPY_MAX_VALUES * sizeof(uint64_t))

struct source_entry {
	enum kernel_entropy_source_status status;
#ifdef KERNEL_ENTROPY_TESTING
	kernel_entropy_test_reader test_reader;
	void *test_context;
	bool test_override;
#endif
};

static bool manager_initialized;
static bool manager_request_active;
static struct source_entry sources[KERNEL_ENTROPY_SOURCE_COUNT];
static uint8_t limine_candidate[LIMINE_BYTES];
static size_t limine_offset;
static size_t limine_length;
static struct spinlock entropy_lock = SPINLOCK_INITIALIZER;

static bool
valid_source(enum kernel_entropy_source source)
{
	return source >= KERNEL_ENTROPY_SOURCE_LIMINE &&
	    source < KERNEL_ENTROPY_SOURCE_COUNT;
}

static uint64_t
entropy_guard_enter(void)
{
	return spinlock_lock_irqsave(&entropy_lock);
}

static void
entropy_guard_leave(uint64_t flags)
{
	spinlock_unlock_irqrestore(&entropy_lock, flags);
}

static bool
valid_boot_material(enum boot_entropy_result result,
	const uint64_t values[BOOT_ENTROPY_MAX_VALUES], size_t count)
{
	switch (result) {
	case BOOT_ENTROPY_PARTIAL_UNTRUSTED:
		return values != NULL && count > 0 &&
		    count < BOOT_ENTROPY_MAX_VALUES;
	case BOOT_ENTROPY_AVAILABLE_UNTRUSTED:
		return values != NULL && count == BOOT_ENTROPY_MAX_VALUES;
	case BOOT_ENTROPY_MISSING:
	case BOOT_ENTROPY_EMPTY:
	case BOOT_ENTROPY_INVALID:
		return count == 0;
	default:
		return false;
	}
}

bool
kernel_entropy_init(enum boot_entropy_result limine_status,
	const uint64_t values[BOOT_ENTROPY_MAX_VALUES], size_t value_count)
{
	if (manager_initialized || !valid_boot_material(limine_status, values,
	    value_count))
		return false;
	for (size_t i = 0; i < KERNEL_ENTROPY_SOURCE_COUNT; i++)
		sources[i].status = KERNEL_ENTROPY_NOT_INITIALIZED;
	for (size_t i = 0; i < sizeof(limine_candidate); i++)
		limine_candidate[i] = 0;
	limine_offset = 0;
	limine_length = 0;
	if (limine_status == BOOT_ENTROPY_PARTIAL_UNTRUSTED ||
	    limine_status == BOOT_ENTROPY_AVAILABLE_UNTRUSTED) {
		const uint8_t *source_bytes = (const uint8_t *)values;
		size_t bytes = value_count * sizeof(*values);
		limine_length = bytes;
		for (size_t i = 0; i < bytes; i++)
			limine_candidate[i] = source_bytes[i];
		sources[KERNEL_ENTROPY_SOURCE_LIMINE].status =
		    KERNEL_ENTROPY_CANDIDATE_AVAILABLE;
	} else if (limine_status == BOOT_ENTROPY_INVALID) {
		sources[KERNEL_ENTROPY_SOURCE_LIMINE].status =
		    KERNEL_ENTROPY_PERMANENTLY_DISABLED;
	} else {
		sources[KERNEL_ENTROPY_SOURCE_LIMINE].status =
		    KERNEL_ENTROPY_UNAVAILABLE;
	}
#ifdef KERNEL_ENTROPY_TESTING
	if (!sources[KERNEL_ENTROPY_SOURCE_RDRAND].test_override)
#endif
	{
		switch (kernel_random_status()) {
		case KERNEL_RANDOM_NOT_INITIALIZED:
			sources[KERNEL_ENTROPY_SOURCE_RDRAND].status =
			    KERNEL_ENTROPY_NOT_INITIALIZED;
			break;
		case KERNEL_RANDOM_UNSUPPORTED:
			sources[KERNEL_ENTROPY_SOURCE_RDRAND].status =
			    KERNEL_ENTROPY_UNSUPPORTED;
			break;
		case KERNEL_RANDOM_AVAILABLE:
			sources[KERNEL_ENTROPY_SOURCE_RDRAND].status =
			    KERNEL_ENTROPY_CANDIDATE_AVAILABLE;
			break;
		case KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE:
			sources[KERNEL_ENTROPY_SOURCE_RDRAND].status =
			    KERNEL_ENTROPY_TEMPORARILY_FAILED;
			break;
		case KERNEL_RANDOM_PERMANENTLY_FAILED:
		default:
			sources[KERNEL_ENTROPY_SOURCE_RDRAND].status =
			    KERNEL_ENTROPY_PERMANENTLY_DISABLED;
			break;
		}
	}
#ifdef KERNEL_ENTROPY_TESTING
	if (!sources[KERNEL_ENTROPY_SOURCE_VIRTIO_RNG].test_override)
#endif
		sources[KERNEL_ENTROPY_SOURCE_VIRTIO_RNG].status =
		    KERNEL_ENTROPY_NOT_INITIALIZED;
	switch (kernel_rdseed_status()) {
	case KERNEL_RDSEED_SUPPORTED:
		sources[KERNEL_ENTROPY_SOURCE_RDSEED].status =
		    KERNEL_ENTROPY_SUPPORTED;
		break;
	case KERNEL_RDSEED_AVAILABLE:
		sources[KERNEL_ENTROPY_SOURCE_RDSEED].status =
		    KERNEL_ENTROPY_CANDIDATE_AVAILABLE;
		break;
	case KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE:
		sources[KERNEL_ENTROPY_SOURCE_RDSEED].status =
		    KERNEL_ENTROPY_TEMPORARILY_FAILED;
		break;
	case KERNEL_RDSEED_UNSUPPORTED:
		sources[KERNEL_ENTROPY_SOURCE_RDSEED].status =
		    KERNEL_ENTROPY_UNSUPPORTED;
		break;
	case KERNEL_RDSEED_NOT_INITIALIZED:
	default:
		sources[KERNEL_ENTROPY_SOURCE_RDSEED].status =
		    KERNEL_ENTROPY_NOT_INITIALIZED;
		break;
	}
	manager_initialized = true;
	return true;
}

enum kernel_entropy_source_status
kernel_entropy_source_state(enum kernel_entropy_source source)
{
	uint64_t flags;
	enum kernel_entropy_source_status status;

	if (!valid_source(source))
		return KERNEL_ENTROPY_INVALID_SOURCE;
	flags = entropy_guard_enter();
	status = manager_initialized ? sources[source].status :
	    KERNEL_ENTROPY_NOT_INITIALIZED;
	entropy_guard_leave(flags);
	return status;
}

bool
kernel_entropy_virtio_probe_complete(bool matching_device_found)
{
	uint64_t flags = entropy_guard_enter();
	if (!manager_initialized || manager_request_active ||
	    sources[KERNEL_ENTROPY_SOURCE_VIRTIO_RNG].status !=
	    KERNEL_ENTROPY_NOT_INITIALIZED) {
		entropy_guard_leave(flags);
		return false;
	}
	if (!matching_device_found) {
		sources[KERNEL_ENTROPY_SOURCE_VIRTIO_RNG].status =
		    KERNEL_ENTROPY_DEVICE_ABSENT;
		entropy_guard_leave(flags);
		return true;
	}
	/* This query is bounded and does not initialize or read the provider. */
	if (virtio_rng_available()) {
		sources[KERNEL_ENTROPY_SOURCE_VIRTIO_RNG].status =
		    KERNEL_ENTROPY_SUPPORTED;
		entropy_guard_leave(flags);
		return true;
	}
	sources[KERNEL_ENTROPY_SOURCE_VIRTIO_RNG].status =
	    KERNEL_ENTROPY_PERMANENTLY_DISABLED;
	entropy_guard_leave(flags);
	return true;
}

bool
kernel_entropy_retry_source(enum kernel_entropy_source source)
{
	uint64_t flags;
	bool available = false;

	if (!valid_source(source))
		return false;
	flags = entropy_guard_enter();
	if (!manager_initialized || manager_request_active ||
	    sources[source].status != KERNEL_ENTROPY_TEMPORARILY_FAILED) {
		entropy_guard_leave(flags);
		return false;
	}
	/* Keep the state protected while querying bounded provider status. */
#ifdef KERNEL_ENTROPY_TESTING
	if (sources[source].test_override)
		available = sources[source].test_reader != NULL;
	else
#endif
	if (source == KERNEL_ENTROPY_SOURCE_RDRAND)
		available = kernel_random_retry();
	else if (source == KERNEL_ENTROPY_SOURCE_VIRTIO_RNG)
		available = virtio_rng_available();
	else if (source == KERNEL_ENTROPY_SOURCE_RDSEED)
		available = kernel_rdseed_supported();
	if (available)
		sources[source].status =
		    (source == KERNEL_ENTROPY_SOURCE_RDSEED ||
		    source == KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) ?
		    KERNEL_ENTROPY_SUPPORTED : KERNEL_ENTROPY_CANDIDATE_AVAILABLE;
	else if ((source == KERNEL_ENTROPY_SOURCE_RDRAND &&
	    kernel_random_status() == KERNEL_RANDOM_PERMANENTLY_FAILED) ||
	    (source == KERNEL_ENTROPY_SOURCE_VIRTIO_RNG &&
	    !virtio_rng_available()))
		sources[source].status = KERNEL_ENTROPY_PERMANENTLY_DISABLED;
	entropy_guard_leave(flags);
	return available;
}

static enum kernel_entropy_result
status_result(enum kernel_entropy_source_status status)
{
	switch (status) {
	case KERNEL_ENTROPY_CANDIDATE_AVAILABLE:
	case KERNEL_ENTROPY_TRUSTED:
	case KERNEL_ENTROPY_SUPPORTED:
		return KERNEL_ENTROPY_OK;
	case KERNEL_ENTROPY_NOT_INITIALIZED:
		return KERNEL_ENTROPY_MANAGER_NOT_INITIALIZED;
	case KERNEL_ENTROPY_TEMPORARILY_FAILED:
		return KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE;
	case KERNEL_ENTROPY_PERMANENTLY_DISABLED:
		return KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE;
	case KERNEL_ENTROPY_UNSUPPORTED:
	case KERNEL_ENTROPY_DEVICE_ABSENT:
	case KERNEL_ENTROPY_UNAVAILABLE:
	case KERNEL_ENTROPY_EXHAUSTED:
	default:
		return KERNEL_ENTROPY_SOURCE_UNAVAILABLE;
	}
}

static enum kernel_entropy_result
read_source(enum kernel_entropy_source source, uint8_t *output,
	size_t capacity, size_t *bytes_written)
{
	*bytes_written = 0;
	if (source == KERNEL_ENTROPY_SOURCE_LIMINE) {
		size_t remaining = limine_length - limine_offset;
		size_t amount = capacity < remaining ? capacity : remaining;
		for (size_t i = 0; i < amount; i++)
			output[i] = limine_candidate[limine_offset + i];
		limine_offset += amount;
		*bytes_written = amount;
		if (limine_offset == limine_length) {
			volatile uint8_t *wipe = limine_candidate;
			for (size_t i = 0; i < sizeof(limine_candidate); i++)
				wipe[i] = 0;
			sources[source].status = KERNEL_ENTROPY_EXHAUSTED;
		}
		return KERNEL_ENTROPY_OK;
	}
#ifdef KERNEL_ENTROPY_TESTING
	if (sources[source].test_override) {
		if (sources[source].test_reader == NULL)
			return KERNEL_ENTROPY_SOURCE_UNAVAILABLE;
		return sources[source].test_reader(sources[source].test_context,
		    output, capacity, bytes_written);
	}
#endif
	if (source == KERNEL_ENTROPY_SOURCE_RDRAND) {
		if (kernel_random_bytes(output, capacity)) {
			*bytes_written = capacity;
			return KERNEL_ENTROPY_OK;
		}
		return kernel_random_status() == KERNEL_RANDOM_PERMANENTLY_FAILED ?
		    KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE :
		    KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE;
	}
	if (source == KERNEL_ENTROPY_SOURCE_RDSEED) {
		switch (kernel_rdseed_bytes(output, capacity)) {
		case KERNEL_RDSEED_OK:
			*bytes_written = capacity;
			return KERNEL_ENTROPY_OK;
		case KERNEL_RDSEED_TEMPORARY_FAILURE:
			return KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE;
		case KERNEL_RDSEED_UNSUPPORTED_RESULT:
			return KERNEL_ENTROPY_SOURCE_UNAVAILABLE;
		case KERNEL_RDSEED_NOT_READY:
			return KERNEL_ENTROPY_MANAGER_NOT_INITIALIZED;
		case KERNEL_RDSEED_INVALID_ARGUMENT:
		default:
			return KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE;
		}
	}
	if (source == KERNEL_ENTROPY_SOURCE_VIRTIO_RNG) {
		switch (virtio_rng_read(output, capacity, bytes_written)) {
		case VIRTIO_RNG_OK:
			return KERNEL_ENTROPY_OK;
		case VIRTIO_RNG_BUSY:
			return KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE;
		case VIRTIO_RNG_UNAVAILABLE:
		case VIRTIO_RNG_TIMEOUT:
		case VIRTIO_RNG_MALFORMED_COMPLETION:
		case VIRTIO_RNG_TRANSPORT_FAILURE:
		default:
			return KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE;
		}
	}
	return KERNEL_ENTROPY_SOURCE_UNAVAILABLE;
}

enum kernel_entropy_result
kernel_entropy_get_candidate(enum kernel_entropy_source source,
	void *output, size_t capacity, struct kernel_entropy_candidate *candidate)
{
	uint8_t temporary[KERNEL_ENTROPY_MAX_REQUEST];
	uint64_t flags;
	size_t bytes_written = 0;
	enum kernel_entropy_result result;
	enum kernel_entropy_source_status status;

	if (candidate != NULL)
		*candidate = (struct kernel_entropy_candidate){
			.source = valid_source(source) ? source :
			    KERNEL_ENTROPY_SOURCE_LIMINE,
			.bytes_written = 0,
			.trust = KERNEL_ENTROPY_UNTRUSTED_CANDIDATE
		};
	if (candidate == NULL || !valid_source(source) ||
	    capacity > KERNEL_ENTROPY_MAX_REQUEST ||
	    (capacity != 0 && output == NULL))
		return KERNEL_ENTROPY_INVALID_ARGUMENT;
	flags = entropy_guard_enter();
	if (!manager_initialized) {
		entropy_guard_leave(flags);
		return KERNEL_ENTROPY_MANAGER_NOT_INITIALIZED;
	}
	if (manager_request_active) {
		entropy_guard_leave(flags);
		return KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE;
	}
	status = sources[source].status;
	result = status_result(status);
	if (result != KERNEL_ENTROPY_OK) {
		entropy_guard_leave(flags);
		return result;
	}
	if (capacity == 0) {
		entropy_guard_leave(flags);
		return KERNEL_ENTROPY_OK;
	}
	manager_request_active = true;
	entropy_guard_leave(flags);
	for (size_t i = 0; i < sizeof(temporary); i++)
		temporary[i] = 0;
	result = read_source(source, temporary, capacity, &bytes_written);
	if (result != KERNEL_ENTROPY_OK &&
	    result != KERNEL_ENTROPY_SOURCE_UNAVAILABLE &&
	    result != KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE &&
	    result != KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE)
		result = KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE;
	if (result == KERNEL_ENTROPY_OK && bytes_written > capacity)
		result = KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE;
	if (result == KERNEL_ENTROPY_OK && bytes_written == 0)
		result = KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE;
	if (result == KERNEL_ENTROPY_OK) {
		uint8_t *destination = output;
		for (size_t i = 0; i < bytes_written; i++)
			destination[i] = temporary[i];
		candidate->bytes_written = bytes_written;
	}
	volatile uint8_t *wipe = temporary;
	for (size_t i = 0; i < sizeof(temporary); i++)
		wipe[i] = 0;
	flags = entropy_guard_enter();
	if (result == KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE)
		sources[source].status = KERNEL_ENTROPY_TEMPORARILY_FAILED;
	else if (result == KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE)
		sources[source].status = KERNEL_ENTROPY_PERMANENTLY_DISABLED;
	else if (result == KERNEL_ENTROPY_SOURCE_UNAVAILABLE)
		sources[source].status = KERNEL_ENTROPY_UNAVAILABLE;
	else if (result == KERNEL_ENTROPY_OK &&
	    source != KERNEL_ENTROPY_SOURCE_LIMINE)
		sources[source].status = KERNEL_ENTROPY_CANDIDATE_AVAILABLE;
	manager_request_active = false;
	entropy_guard_leave(flags);
	return result;
}

bool
kernel_entropy_cryptographic_ready(void)
{
	return false;
}

#ifdef KERNEL_ENTROPY_TESTING
bool
kernel_entropy_test_set_source(enum kernel_entropy_source source,
	enum kernel_entropy_source_status status,
	kernel_entropy_test_reader reader, void *context)
{
	if (!valid_source(source) || status == KERNEL_ENTROPY_TRUSTED ||
	    status > KERNEL_ENTROPY_TRUSTED ||
	    (status == KERNEL_ENTROPY_CANDIDATE_AVAILABLE && reader == NULL))
		return false;
	sources[source].status = status;
	sources[source].test_reader = reader;
	sources[source].test_context = context;
	sources[source].test_override = true;
	return true;
}

void
kernel_entropy_test_reset(void)
{
	volatile uint8_t *wipe = limine_candidate;
	for (size_t i = 0; i < sizeof(limine_candidate); i++)
		wipe[i] = 0;
	manager_initialized = false;
	manager_request_active = false;
	limine_offset = 0;
	limine_length = 0;
	for (size_t i = 0; i < KERNEL_ENTROPY_SOURCE_COUNT; i++)
		sources[i] = (struct source_entry){0};
}
#endif
