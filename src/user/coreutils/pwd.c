#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	char path[256];
	long result;
	(void)argv;
	if (argc != 1) {
		u_puts("pwd: options are unsupported\n");
		return 1;
	}
	result = u_getcwd(path, sizeof(path));
	if (result < 0) {
		u_puts("pwd: getcwd failed\n");
		return 1;
	}
	u_puts(path);
	u_puts("\n");
	return 0;
}
