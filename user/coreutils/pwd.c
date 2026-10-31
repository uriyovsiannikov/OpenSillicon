#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	(void)argv;
	if (argc != 1) {
		u_puts("pwd: options are unsupported\n");
		return 1;
	}
	return u_write(1, "/\n", 2) == 2 ? 0 : 1;
}
