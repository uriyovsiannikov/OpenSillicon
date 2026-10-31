#include "include/os.h"

int
user_main(uint64_t argc, char **argv, const uint64_t *startup_stack)
{
	(void)argc;
	(void)argv;
	(void)startup_stack;
	__asm__ volatile ("ud2");
	return 1;
}
