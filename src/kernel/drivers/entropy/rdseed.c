#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/rdseed.h"
#include "kernel/spinlock.h"

#define CPUID_RDSEED (1U << 18)

static bool init_attempted;
static bool feature_supported;
static enum kernel_rdseed_status provider_status =
	KERNEL_RDSEED_NOT_INITIALIZED;
static bool request_active;
static struct spinlock rdseed_lock = SPINLOCK_INITIALIZER;

#ifdef RDSEED_TESTING
static kernel_rdseed_test_cpuid_fn test_cpuid;
static kernel_rdseed_test_instruction_fn test_instruction;
static void *test_context;
#endif

static uint64_t
rdseed_guard_enter(void)
{
	return spinlock_lock_irqsave(&rdseed_lock);
}

static void
rdseed_guard_leave(uint64_t flags)
{
	spinlock_unlock_irqrestore(&rdseed_lock, flags);
}

static void
zeroize(void *buffer, size_t length)
{
	volatile uint8_t *bytes = buffer;
	while (length-- != 0)
		*bytes++ = 0;
}

static bool
cpuid_query(uint32_t leaf, uint32_t subleaf,
	struct kernel_rdseed_cpuid_result *result)
{
#ifdef RDSEED_TESTING
	if (test_cpuid != NULL)
		return test_cpuid(test_context, leaf, subleaf, result);
#endif
#ifdef OPEN_SILLICON_KERNEL
	uint32_t eax = leaf, ebx, ecx = subleaf, edx;
	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "+c" (ecx),
	    "=d" (edx) : : "memory");
	*result = (struct kernel_rdseed_cpuid_result){eax, ebx, ecx, edx};
	return true;
#else
	(void)leaf;
	(void)subleaf;
	(void)result;
	return false;
#endif
}

static bool
rdseed_instruction(uint64_t *value, bool *carry)
{
#ifdef RDSEED_TESTING
	if (test_instruction != NULL)
		return test_instruction(test_context, value, carry);
#endif
#ifdef OPEN_SILLICON_KERNEL
	uint64_t result;
	unsigned char success;
	/* Keep the instruction and CF capture adjacent. Never inspect result when
	 * CF is clear. The early-clobber prevents register overlap with inputs. */
	__asm__ volatile ("rdseed %0; setc %1" : "=&r" (result), "=qm" (success)
	    : : "cc");
	if (success == 0)
		return false;
	*value = result;
	*carry = true;
	return true;
#else
	(void)value;
	(void)carry;
	return false;
#endif
}

static bool
initialize_locked(void)
{
	struct kernel_rdseed_cpuid_result result = {0};
	uint32_t maximum_leaf;

	if (init_attempted)
		return feature_supported;
	init_attempted = true;
	if (!cpuid_query(0, 0, &result)) {
		provider_status = KERNEL_RDSEED_UNSUPPORTED;
		return false;
	}
	maximum_leaf = result.eax;
	if (maximum_leaf < 7U || !cpuid_query(7, 0, &result) ||
	    (result.ebx & CPUID_RDSEED) == 0) {
		provider_status = KERNEL_RDSEED_UNSUPPORTED;
		return false;
	}
	feature_supported = true;
	/* CPUID establishes only that an attempt is legal, not that the hardware
	 * will produce a word now. AVAILABLE is set only after CF=1. */
	provider_status = KERNEL_RDSEED_SUPPORTED;
	return true;
}

bool
kernel_rdseed_init(void)
{
	uint64_t flags = rdseed_guard_enter();
	bool supported = initialize_locked();
	rdseed_guard_leave(flags);
	return supported;
}

bool
kernel_rdseed_supported(void)
{
	uint64_t flags = rdseed_guard_enter();
	bool supported = feature_supported;
	rdseed_guard_leave(flags);
	return supported;
}

enum kernel_rdseed_status
kernel_rdseed_status(void)
{
	uint64_t flags = rdseed_guard_enter();
	enum kernel_rdseed_status status = provider_status;
	rdseed_guard_leave(flags);
	return status;
}

static enum kernel_rdseed_result
word_locked(uint64_t *output)
{
	for (unsigned int attempt = 0; attempt < KERNEL_RDSEED_RETRY_LIMIT;
	    attempt++) {
		uint64_t value = 0;
		bool carry = false;
		bool invoked = rdseed_instruction(&value, &carry);
		if (invoked && carry) {
			*output = value;
			zeroize(&value, sizeof(value));
			provider_status = KERNEL_RDSEED_AVAILABLE;
			return KERNEL_RDSEED_OK;
		}
		/* A failed instruction's destination is architecturally invalid. */
		zeroize(&value, sizeof(value));
		__asm__ volatile ("pause" : : : "memory");
	}
	provider_status = KERNEL_RDSEED_TEMPORARILY_UNAVAILABLE;
	return KERNEL_RDSEED_TEMPORARY_FAILURE;
}

enum kernel_rdseed_result
kernel_rdseed_word(uint64_t *output)
{
	uint64_t flags;
	enum kernel_rdseed_result result;
	if (output == NULL)
		return KERNEL_RDSEED_INVALID_ARGUMENT;
	flags = rdseed_guard_enter();
	if (!init_attempted) {
		result = KERNEL_RDSEED_NOT_READY;
	} else if (!feature_supported) {
		result = KERNEL_RDSEED_UNSUPPORTED_RESULT;
	} else if (__atomic_exchange_n(&request_active, true,
	    __ATOMIC_ACQUIRE)) {
		result = KERNEL_RDSEED_TEMPORARY_FAILURE;
	} else {
		result = word_locked(output);
		__atomic_store_n(&request_active, false, __ATOMIC_RELEASE);
	}
	rdseed_guard_leave(flags);
	return result;
}

enum kernel_rdseed_result
kernel_rdseed_bytes(void *output, size_t length)
{
	uint8_t temporary[KERNEL_RDSEED_MAX_REQUEST];
	uint64_t flags;
	enum kernel_rdseed_result result = KERNEL_RDSEED_OK;

	if ((length != 0 && output == NULL) ||
	    length > KERNEL_RDSEED_MAX_REQUEST)
		return KERNEL_RDSEED_INVALID_ARGUMENT;
	flags = rdseed_guard_enter();
	if (!init_attempted) {
		result = KERNEL_RDSEED_NOT_READY;
		goto done;
	}
	if (!feature_supported) {
		result = KERNEL_RDSEED_UNSUPPORTED_RESULT;
		goto done;
	}
	if (length == 0)
		goto done;
	if (__atomic_exchange_n(&request_active, true, __ATOMIC_ACQUIRE)) {
		result = KERNEL_RDSEED_TEMPORARY_FAILURE;
		goto done;
	}
	for (size_t offset = 0; offset < length; ) {
		uint64_t word = 0;
		result = word_locked(&word);
		if (result != KERNEL_RDSEED_OK) {
			zeroize(&word, sizeof(word));
			break;
		}
		size_t count = length - offset;
		if (count > sizeof(word))
			count = sizeof(word);
		for (size_t i = 0; i < count; i++)
			temporary[offset + i] = (uint8_t)(word >> (i * 8U));
		offset += count;
		zeroize(&word, sizeof(word));
	}
	if (result == KERNEL_RDSEED_OK) {
		uint8_t *destination = output;
		for (size_t i = 0; i < length; i++)
			destination[i] = temporary[i];
	}
	zeroize(temporary, sizeof(temporary));
	__atomic_store_n(&request_active, false, __ATOMIC_RELEASE);
done:
	rdseed_guard_leave(flags);
	return result;
}

#ifdef RDSEED_TESTING
bool
kernel_rdseed_test_set_backend(kernel_rdseed_test_cpuid_fn cpuid,
	kernel_rdseed_test_instruction_fn instruction, void *context)
{
	if (init_attempted || cpuid == NULL || instruction == NULL)
		return false;
	test_cpuid = cpuid;
	test_instruction = instruction;
	test_context = context;
	return true;
}

void
kernel_rdseed_test_reset(void)
{
	init_attempted = false;
	feature_supported = false;
	provider_status = KERNEL_RDSEED_NOT_INITIALIZED;
	__atomic_store_n(&request_active, false, __ATOMIC_RELEASE);
	test_cpuid = NULL;
	test_instruction = NULL;
	test_context = NULL;
}
#endif
