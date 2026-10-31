#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

static __thread unsigned long initialized_tls = 0x4d55534c76543210UL;
static __thread volatile unsigned long zero_tls;

int
main(int argc, char **argv, char **envp)
{
	static const char message[] =
	    "OpenSillicon: musl mallocng small/large allocations and mmap passed\n";
	unsigned char *small_a;
	unsigned char *small_b;
	unsigned char *zeroed;
	unsigned char *grown;
	unsigned char *large;
	unsigned char *larger;
	unsigned char *mapping;
	size_t i;
	ssize_t written;

	if (argc != 0 || argv == NULL || argv[0] != NULL || envp == NULL ||
	    envp[0] != NULL || initialized_tls != 0x4d55534c76543210UL ||
	    zero_tls != 0)
		return 21;

	small_a = malloc(37);
	small_b = malloc(91);
	if (small_a == NULL || small_b == NULL ||
	    ((uintptr_t)small_a < (uintptr_t)small_b &&
	    (uintptr_t)small_b < (uintptr_t)small_a + 37) ||
	    ((uintptr_t)small_b < (uintptr_t)small_a &&
	    (uintptr_t)small_a < (uintptr_t)small_b + 91))
		return 22;
	memset(small_a, 0x35, 37);
	memset(small_b, 0x6b, 91);
	free(small_a);
	if (small_b[0] != 0x6b || small_b[90] != 0x6b)
		return 23;
	free(small_b);

	zeroed = calloc(128, 3);
	if (zeroed == NULL)
		return 24;
	for (i = 0; i < 128 * 3; i++) {
		if (zeroed[i] != 0)
			return 25;
	}
	memcpy(zeroed, "musl realloc", 13);
	grown = realloc(zeroed, 4096);
	if (grown == NULL || memcmp(grown, "musl realloc", 13) != 0)
		return 26;
	free(grown);

	large = malloc(200000);
	if (large == NULL)
		return 27;
	for (i = 0; i < 200000; i += 4096)
		large[i] = (unsigned char)(i / 4096 + 1);
	larger = realloc(large, 300000);
	if (larger == NULL)
		return 28;
	for (i = 0; i < 200000; i += 4096) {
		if (larger[i] != (unsigned char)(i / 4096 + 1))
			return 29;
	}
	memset(larger + 200000, 0xa7, 100000);
	free(larger);

	for (i = 0; i < 24; i++) {
		void *cycle = malloc(70000 + i * 17);

		if (cycle == NULL)
			return 30;
		memset(cycle, (int)i, 70000 + i * 17);
		free(cycle);
	}

	errno = 0;
	if (mmap(NULL, 0, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED || errno != EINVAL)
		return 31;
	errno = 0;
	if (mmap(NULL, SIZE_MAX, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED || errno != ENOMEM)
		return 57;
	errno = 0;
	if (syscall(SYS_mmap, NULL, SIZE_MAX, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != -1 || errno != EINVAL)
		return 60;
	errno = 0;
	if (mmap((void *)(uintptr_t)0x400000, 4096, PROT_READ,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED || errno != EINVAL)
		return 32;
	errno = 0;
	if (mmap(NULL, 4096, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0) !=
	    MAP_FAILED || errno != EINVAL)
		return 33;
	errno = 0;
	if (mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0) !=
	    MAP_FAILED || errno != EINVAL)
		return 34;
	errno = 0;
	if (mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 4096) !=
	    MAP_FAILED || errno != EINVAL)
		return 35;
	errno = 0;
	if (mmap(NULL, 4096, PROT_READ | PROT_EXEC,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
	    errno != EOPNOTSUPP)
		return 36;
	errno = 0;
	if (mmap(NULL, 4096, PROT_READ | 0x2000,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED || errno != EINVAL)
		return 49;
	errno = 0;
	if (mmap(NULL, 4096, PROT_READ,
	    MAP_PRIVATE | MAP_ANONYMOUS | 0x40000000, -1, 0) != MAP_FAILED ||
	    errno != EINVAL)
		return 50;
	errno = 0;
	if (mmap(NULL, 0x100000000000ULL, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != MAP_FAILED || errno != ENOMEM)
		return 37;
	mapping = mmap(NULL, 4096, PROT_READ,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || *mapping != 0 ||
	    mprotect(mapping, 4096, PROT_READ | PROT_WRITE) != 0)
		return 51;
	*mapping = 0x5a;
	if (*mapping != 0x5a || munmap(mapping, 4096) != 0)
		return 52;
	mapping = mmap(NULL, 4096, PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || mprotect(mapping, 4096, PROT_READ) != 0 ||
	    *mapping != 0 || munmap(mapping, 4096) != 0)
		return 53;

	mapping = mmap(NULL, 8193, PROT_NONE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED || ((uintptr_t)mapping & 4095U) != 0)
		return 38;
	if (mprotect(mapping + 4096, 4096, PROT_READ | PROT_WRITE) != 0)
		return 39;
	*(volatile uint64_t *)(void *)(mapping + 4096) = 0x4d4d41505f544553ULL;
	if (mprotect(mapping + 4096, 4096, PROT_NONE) != 0 ||
	    mprotect(mapping + 4096, 4096, PROT_READ | PROT_WRITE) != 0 ||
	    *(volatile uint64_t *)(void *)(mapping + 4096) !=
	    0x4d4d41505f544553ULL)
		return 40;
	errno = 0;
	if (mprotect(mapping, 4096, PROT_EXEC) != -1 || errno != EOPNOTSUPP)
		return 41;
	errno = 0;
	if (mprotect(mapping, 0, PROT_READ) != -1 || errno != EINVAL)
		return 42;
	errno = 0;
	if (mprotect(mapping, 16384, PROT_READ) != -1 || errno != EINVAL)
		return 54;
	errno = 0;
	if (mprotect(mapping, SIZE_MAX, PROT_READ) != -1 || errno != EINVAL)
		return 58;
	errno = 0;
	if (mprotect(mapping, 4096, 0x2000) != -1 || errno != EINVAL)
		return 55;
	errno = 0;
	if (mprotect((void *)(uintptr_t)0x400000, 4096, PROT_READ) != -1 ||
	    errno != EINVAL)
		return 56;
	errno = 0;
	if (munmap(mapping, 4096) != -1 || errno != EINVAL)
		return 43;
	errno = 0;
	if (munmap(mapping, 16384) != -1 || errno != EINVAL)
		return 44;
	errno = 0;
	if (munmap(mapping, SIZE_MAX) != -1 || errno != EINVAL)
		return 59;
	if (mprotect(mapping, 8193, PROT_READ | PROT_WRITE) != 0 ||
	    munmap(mapping, 12288) != 0)
		return 45;
	errno = 0;
	if (munmap(mapping, 12288) != -1 || errno != EINVAL)
		return 46;

	if (initialized_tls != 0x4d55534c76543210UL || zero_tls != 0)
		return 47;
	initialized_tls++;
	written = write(STDOUT_FILENO, message, sizeof(message) - 1);
	if (written != (ssize_t)(sizeof(message) - 1) || initialized_tls !=
	    0x4d55534c76543211UL || zero_tls != 0)
		return 48;
	return 0;
}
