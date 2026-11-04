#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "arch/x86_64/copy.h"

int
main(void)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t capacity = page * 2;
	uint8_t *src = mmap(NULL, page * 4, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint8_t *dst = mmap(NULL, page * 4, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(src != MAP_FAILED && dst != MAP_FAILED);
	assert(mprotect(src + page, capacity, PROT_READ | PROT_WRITE) == 0);
	assert(mprotect(dst + page, capacity, PROT_READ | PROT_WRITE) == 0);
	for (size_t i = 0; i < capacity; i++)
		src[page + i] = (uint8_t)(i * 17);
	assert(mprotect(src + page, capacity, PROT_READ) == 0);

	for (size_t length = 0; length <= capacity; length++) {
		/* End at a guard page to catch any overread or overwrite. */
		uint8_t *s = src + page + capacity - length;
		uint8_t *d = dst + page + capacity - length;
		memset(dst + page, 0xa5, capacity);
		x86_copy_ram(d, s, length);
		assert(memcmp(d, s, length) == 0);
		if (length < capacity)
			assert(d[-1] == 0xa5);
	}
	for (size_t s = 0; s < 16; s++)
		for (size_t d = 0; d < 16; d++)
			for (size_t length = 0; length < 257; length++) {
				memset(dst + page, 0xa5, 512);
				x86_copy_ram(dst + page + d, src + page + s, length);
				assert(memcmp(dst + page + d, src + page + s, length) == 0);
				assert(dst[page + d + length] == 0xa5);
				if (d != 0)
					assert(dst[page + d - 1] == 0xa5);
			}
	assert(munmap(src, page * 4) == 0);
	assert(munmap(dst, page * 4) == 0);
	puts("RAM copy: alignments, tails and guard-page boundaries passed");
	return 0;
}
