#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	static const char sequence[] = "\033[0m\033[2J\033[H";
	(void)argv;
	if (argc != 1) {
		u_puts("clear: arguments are unsupported\n");
		return 1;
	}
	return u_write(1, sequence, sizeof(sequence) - 1) ==
	    (long)(sizeof(sequence) - 1) ? 0 : 1;
}
