#ifndef KERNEL_ENTROPY_H
#define KERNEL_ENTROPY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/boot_entropy.h"

#define KERNEL_ENTROPY_MAX_REQUEST 256U

/* Manager operations are for serialized normal kernel context, not interrupt
 * handlers. The current busy guard and local interrupt masking implement the
 * project's single-CPU convention only; they are not an SMP lock. */

enum kernel_entropy_source {
	KERNEL_ENTROPY_SOURCE_LIMINE = 0,
	KERNEL_ENTROPY_SOURCE_RDRAND,
	KERNEL_ENTROPY_SOURCE_VIRTIO_RNG,
	KERNEL_ENTROPY_SOURCE_RDSEED,
	KERNEL_ENTROPY_SOURCE_COUNT
};

enum kernel_entropy_source_status {
	KERNEL_ENTROPY_NOT_INITIALIZED = 0,
	KERNEL_ENTROPY_INVALID_SOURCE,
	KERNEL_ENTROPY_UNSUPPORTED,
	KERNEL_ENTROPY_DEVICE_ABSENT,
	KERNEL_ENTROPY_SUPPORTED, /* bounded acquisition may be attempted */
	KERNEL_ENTROPY_UNAVAILABLE,
	KERNEL_ENTROPY_CANDIDATE_AVAILABLE,
	KERNEL_ENTROPY_TEMPORARILY_FAILED,
	KERNEL_ENTROPY_PERMANENTLY_DISABLED,
	KERNEL_ENTROPY_EXHAUSTED, /* one-shot source consumed; not retryable */
	KERNEL_ENTROPY_TRUSTED
};

enum kernel_entropy_trust {
	KERNEL_ENTROPY_UNTRUSTED_CANDIDATE = 0,
	KERNEL_ENTROPY_TRUSTED_CANDIDATE
};

enum kernel_entropy_result {
	KERNEL_ENTROPY_OK = 0,
	KERNEL_ENTROPY_INVALID_ARGUMENT,
	KERNEL_ENTROPY_MANAGER_NOT_INITIALIZED,
	KERNEL_ENTROPY_SOURCE_UNAVAILABLE,
	KERNEL_ENTROPY_SOURCE_TEMPORARY_FAILURE,
	KERNEL_ENTROPY_SOURCE_PERMANENT_FAILURE
};

struct kernel_entropy_candidate {
	enum kernel_entropy_source source;
	size_t bytes_written;
	enum kernel_entropy_trust trust;
};

/* One-shot, allocation-free initialization. limine_status and values must be
 * the already validated result of boot_entropy_inspect(); the manager copies
 * at most its four returned words and retains no Limine pointers. A malformed
 * Limine response is recorded as permanently disabled, not a boot failure.
 * RDRAND/RDSEED capability is observed, not promoted to trusted entropy;
 * kernel_rdseed_init() must already have run for RDSEED status discovery.
 * VirtIO remains NOT_INITIALIZED until kernel_entropy_virtio_probe_complete(). */
bool kernel_entropy_init(enum boot_entropy_result limine_status,
	const uint64_t values[BOOT_ENTROPY_MAX_VALUES], size_t value_count);

enum kernel_entropy_source_status kernel_entropy_source_state(
	enum kernel_entropy_source source);

/* Records the result of the explicitly gated VirtIO device probe. If no
 * matching device was found, status becomes DEVICE_ABSENT. A ready driver is
 * SUPPORTED (bounded read may be attempted), not CANDIDATE_AVAILABLE until a
 * read succeeds. Failed/ambiguous setup is permanently disabled for this
 * boot. This function never initializes or reads a device. */
bool kernel_entropy_virtio_probe_complete(bool matching_device_found);

/* Explicit caller-driven bounded retry after a temporary source failure.
 * RDSEED re-arms an explicit later read; RDRAND performs its bounded retry
 * probe; VirtIO only rechecks driver availability. It never clears a
 * permanently-disabled state or selects another source. */
bool kernel_entropy_retry_source(enum kernel_entropy_source source);

/* Reads from exactly the selected source; there is no fallback. Requests are
 * bounded by KERNEL_ENTROPY_MAX_REQUEST. Candidate metadata is initialized
 * deterministically on every call when non-null. On failure, output is left
 * unchanged. A successful source may return fewer bytes than requested;
 * bytes_written reports the exact count. Zero-length requests are successful
 * no-ops for an available source and may use a null output pointer. */
enum kernel_entropy_result kernel_entropy_get_candidate(
	enum kernel_entropy_source source, void *output, size_t capacity,
	struct kernel_entropy_candidate *candidate);

/* This is intentionally false: candidate source availability is not
 * cryptographic readiness, and this phase has no approved trust policy or
 * CSPRNG. */
bool kernel_entropy_cryptographic_ready(void);

#ifdef KERNEL_ENTROPY_TESTING
typedef enum kernel_entropy_result (*kernel_entropy_test_reader)(
	void *context, uint8_t *output, size_t capacity, size_t *bytes_written);
bool kernel_entropy_test_set_source(enum kernel_entropy_source source,
	enum kernel_entropy_source_status status,
	kernel_entropy_test_reader reader, void *context);
void kernel_entropy_test_reset(void);
#endif

#endif
