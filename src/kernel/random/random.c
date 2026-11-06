#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/random.h"

#define CPUID_RDRAND (1U << 30)
#define RDRAND_RETRY_LIMIT 10U

static bool init_attempted;
static bool source_ready;
static bool feature_supported;
static enum kernel_random_source_status provider_status =
	KERNEL_RANDOM_NOT_INITIALIZED;
static bool request_active;

#ifdef KERNEL_RANDOM_TEST
static bool test_source_active;
static kernel_random_test_word_fn test_source;
static void *test_source_context;
#endif

static uint64_t
random_guard_enter(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
#else
	return 0;
#endif
}

static void
random_guard_leave(uint64_t flags)
{
#ifdef OPEN_SILLICON_KERNEL
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
#else
	(void)flags;
#endif
}

static void
zeroize(void *buffer, size_t length)
{
	volatile uint8_t *bytes = buffer;
	while (length-- != 0)
		*bytes++ = 0;
}

static bool
rdrand_supported(void)
{
	uint32_t eax = 1;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	__asm__ volatile ("cpuid" : "+a" (eax), "=b" (ebx), "=c" (ecx),
	    "=d" (edx) : : "memory");
	(void)ebx;
	(void)edx;
	return (ecx & CPUID_RDRAND) != 0;
}

static bool
rdrand_word(uint64_t *word)
{
	unsigned char success;
	uint64_t value;
	__asm__ volatile ("rdrand %0; setc %1" : "=&r" (value), "=qm" (success)
	    : : "cc");
	if (success == 0)
		return false;
	*word = value;
	return true;
}

static bool
source_word(uint64_t *word)
{
#ifdef KERNEL_RANDOM_TEST
	if (test_source_active)
		return test_source(test_source_context, word);
#endif
	return rdrand_word(word);
}

static bool
source_word_bounded(uint64_t *word)
{
	for (unsigned int attempt = 0; attempt < RDRAND_RETRY_LIMIT; attempt++) {
		if (source_word(word))
			return true;
		__asm__ volatile ("pause" : : : "memory");
	}
	return false;
}

static bool
initialize_locked(void)
{
	uint64_t probe = 0;
	if (init_attempted)
		return provider_status == KERNEL_RANDOM_AVAILABLE;
	init_attempted = true;
#ifdef KERNEL_RANDOM_TEST
	if (!test_source_active && !rdrand_supported()) {
#else
	if (!rdrand_supported()) {
#endif
		provider_status = KERNEL_RANDOM_UNSUPPORTED;
		return false;
	}
	feature_supported = true;
	if (!source_word_bounded(&probe)) {
		provider_status = KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE;
		zeroize(&probe, sizeof(probe));
		return false;
	}
	/* RDRAND is itself the CPU's DRBG output; this probe is discarded. */
	zeroize(&probe, sizeof(probe));
	source_ready = true;
	provider_status = KERNEL_RANDOM_AVAILABLE;
	return true;
}

bool
kernel_random_init(void)
{
	uint64_t flags = random_guard_enter();
	bool ready = initialize_locked();
	random_guard_leave(flags);
	return ready;
}

bool
kernel_random_retry(void)
{
	uint64_t flags = random_guard_enter();
	uint64_t probe = 0;
	bool ready = false;
	if (init_attempted && feature_supported &&
	    provider_status == KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE &&
	    !__atomic_exchange_n(&request_active, true, __ATOMIC_ACQUIRE)) {
		if (source_word_bounded(&probe)) {
			source_ready = true;
			provider_status = KERNEL_RANDOM_AVAILABLE;
			ready = true;
		} else {
			source_ready = false;
			provider_status = KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE;
		}
		zeroize(&probe, sizeof(probe));
		__atomic_store_n(&request_active, false, __ATOMIC_RELEASE);
	}
	random_guard_leave(flags);
	return ready;
}

bool
kernel_random_available(void)
{
	uint64_t flags = random_guard_enter();
	bool ready = source_ready &&
	    provider_status == KERNEL_RANDOM_AVAILABLE;
	random_guard_leave(flags);
	return ready;
}

enum kernel_random_source_status
kernel_random_status(void)
{
	uint64_t flags = random_guard_enter();
	enum kernel_random_source_status status = provider_status;
	random_guard_leave(flags);
	return status;
}

bool
kernel_random_bytes(void *output, size_t length)
{
	uint8_t temporary[KERNEL_RANDOM_MAX_REQUEST];
	uint64_t flags;
	bool success = true;

	if ((length != 0 && output == NULL) ||
	    length > KERNEL_RANDOM_MAX_REQUEST)
		return false;
	flags = random_guard_enter();
	if (!source_ready || provider_status != KERNEL_RANDOM_AVAILABLE ||
	    __atomic_exchange_n(&request_active, true, __ATOMIC_ACQUIRE)) {
		random_guard_leave(flags);
		return false;
	}
	for (size_t offset = 0; offset < length; ) {
		uint64_t word = 0;
		size_t amount = length - offset;
		if (amount > sizeof(word))
			amount = sizeof(word);
		if (!source_word_bounded(&word)) {
			source_ready = false;
			provider_status = KERNEL_RANDOM_TEMPORARILY_UNAVAILABLE;
			success = false;
			zeroize(&word, sizeof(word));
			break;
		}
		for (size_t i = 0; i < amount; i++)
			temporary[offset + i] = (uint8_t)(word >> (i * 8U));
		offset += amount;
		zeroize(&word, sizeof(word));
	}
	if (success) {
		uint8_t *destination = output;
		for (size_t i = 0; i < length; i++)
			destination[i] = temporary[i];
	}
	zeroize(temporary, sizeof(temporary));
	__atomic_store_n(&request_active, false, __ATOMIC_RELEASE);
	random_guard_leave(flags);
	return success;
}

#ifdef KERNEL_RANDOM_TEST
bool
kernel_random_test_init_source(kernel_random_test_word_fn source,
	void *context)
{
	uint64_t flags = random_guard_enter();
	bool ready;
	if (init_attempted || source == NULL) {
		random_guard_leave(flags);
		return false;
	}
	test_source = source;
	test_source_context = context;
	test_source_active = true;
	ready = initialize_locked();
	random_guard_leave(flags);
	return ready;
}

void
kernel_random_test_reset(void)
{
	uint64_t flags = random_guard_enter();
	init_attempted = false;
	source_ready = false;
	feature_supported = false;
	provider_status = KERNEL_RANDOM_NOT_INITIALIZED;
	__atomic_store_n(&request_active, false, __ATOMIC_RELEASE);
	test_source_active = false;
	test_source = NULL;
	test_source_context = NULL;
	random_guard_leave(flags);
}
#endif
