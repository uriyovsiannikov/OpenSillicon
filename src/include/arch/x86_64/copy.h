#ifndef ARCH_X86_64_COPY_H
#define ARCH_X86_64_COPY_H

#include <stddef.h>

/* Copy ordinary RAM after its mappings have been validated.  Each access
 * stays within length, including for unaligned, page-ending buffers.
 * Interrupt entry clears DF; no SIMD registers are used in kernel code. */
static inline void
x86_copy_ram(void *destination, const void *source, size_t length)
{
	size_t words = length / 8;
	size_t tail = length % 8;

	__asm__ volatile ("rep movsq" : "+D" (destination), "+S" (source),
	    "+c" (words) : : "memory");
	__asm__ volatile ("rep movsb" : "+D" (destination), "+S" (source),
	    "+c" (tail) : : "memory");
}

#endif
