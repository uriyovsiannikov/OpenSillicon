#ifndef KERNEL_RDSEED_H
#define KERNEL_RDSEED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KERNEL_RDSEED_MAX_REQUEST 256U
#define KERNEL_RDSEED_RETRY_LIMIT 10U

enum kernel_rdseed_status {
	KERNEL_RDSEED_NOT_INITIALIZED = 0,
	KERNEL_RDSEED_UNSUPPORTED,
	KERNEL_RDSEED_SUPPORTED,
	KERNEL_RDSEED_AVAILABLE,
	KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE
};

enum kernel_rdseed_result {
	KERNEL_RDSEED_OK = 0,
	KERNEL_RDSEED_INVALID_ARGUMENT,
	KERNEL_RDSEED_NOT_READY,
	KERNEL_RDSEED_UNSUPPORTED_RESULT,
	KERNEL_RDSEED_TEMPORARY_FAILURE
};

struct kernel_rdseed_cpuid_result {
	uint32_t eax, ebx, ecx, edx;
};

/* One-shot CPUID detection. This only discovers architectural support; it
 * does not execute RDSEED or establish entropy quality. Status SUPPORTED
 * means an instruction attempt is permitted. AVAILABLE is set only after a
 * recent CF=1 result and does not guarantee the next request will succeed. */
bool kernel_rdseed_init(void);
bool kernel_rdseed_supported(void);
enum kernel_rdseed_status kernel_rdseed_status(void);

/* Reads one 64-bit word, preserving output on every failure. A zero word is
 * valid when the instruction reports CF=1. Retry exhaustion is temporary. */
enum kernel_rdseed_result kernel_rdseed_word(uint64_t *output);

/* Bounded, all-or-nothing output. Length is at most 256 bytes; output is
 * unchanged on failure. A zero-length request is a no-op after initialization
 * and feature detection. Not for interrupt context; current kernel is UP. */
enum kernel_rdseed_result kernel_rdseed_bytes(void *output, size_t length);

#ifdef RDSEED_TESTING
typedef bool (*kernel_rdseed_test_cpuid_fn)(void *context, uint32_t leaf,
	uint32_t subleaf, struct kernel_rdseed_cpuid_result *result);
typedef bool (*kernel_rdseed_test_instruction_fn)(void *context,
	uint64_t *value, bool *carry);
bool kernel_rdseed_test_set_backend(kernel_rdseed_test_cpuid_fn cpuid,
	kernel_rdseed_test_instruction_fn instruction, void *context);
void kernel_rdseed_test_reset(void);
#endif

#endif
