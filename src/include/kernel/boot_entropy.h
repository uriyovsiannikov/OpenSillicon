#ifndef KERNEL_BOOT_ENTROPY_H
#define KERNEL_BOOT_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#include "limine.h"

#define BOOT_ENTROPY_MAX_VALUES 4U

enum boot_entropy_result {
	BOOT_ENTROPY_MISSING = 0,
	BOOT_ENTROPY_EMPTY,
	BOOT_ENTROPY_PARTIAL_UNTRUSTED,
	BOOT_ENTROPY_AVAILABLE_UNTRUSTED,
	BOOT_ENTROPY_INVALID
};

/* Copies at most BOOT_ENTROPY_MAX_VALUES Limine words to kernel-owned output.
 * Limine labels this source best-effort; every non-error result is untrusted
 * and must not be used as a cryptographic seed without a separate policy. On
 * failure, a non-null output is cleared; a non-null value_count is set to zero. */
enum boot_entropy_result boot_entropy_inspect(
	const struct limine_entropy_response *response,
	uint64_t hhdm_offset,
	const struct limine_memmap_response *memmap,
	uint64_t output[BOOT_ENTROPY_MAX_VALUES], size_t *value_count);

#endif
