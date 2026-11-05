#include <stdlib.h>

/* Small deterministic POSIX-style PRNG; this is not suitable for secrets. */
static unsigned long random_state = 1;

void
srand(unsigned int seed)
{
	random_state = seed;
}

int
rand(void)
{
	random_state = (1103515245UL * random_state + 12345UL) & RAND_MAX;
	return (int)random_state;
}
long random(void) { return rand(); }
void srandom(unsigned int seed) { srand(seed); }
