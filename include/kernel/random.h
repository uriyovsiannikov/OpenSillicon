#ifndef KERNEL_RANDOM_H
#define KERNEL_RANDOM_H

#include <stdbool.h>
#include <stddef.h>

#define KERNEL_RANDOM_MAX_REQUEST 256U

/* This interface is available only when the CPU exposes a healthy RDRAND
 * source. It must not be called from interrupt context. */
bool kernel_random_init(void);
bool kernel_random_available(void);
bool kernel_random_bytes(void *output, size_t length);

#ifdef KERNEL_RANDOM_TEST
#include <stdint.h>
typedef bool (*kernel_random_test_word_fn)(void *context, uint64_t *word);
bool kernel_random_test_init_source(kernel_random_test_word_fn source,
	void *context);
void kernel_random_test_reset(void);
#endif

#endif
