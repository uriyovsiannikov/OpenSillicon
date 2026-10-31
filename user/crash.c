#include "include/os.h"

int
user_main(const uint64_t *startup_stack)
{
	(void)startup_stack;
	__asm__ volatile ("ud2");
	return 1;
}
