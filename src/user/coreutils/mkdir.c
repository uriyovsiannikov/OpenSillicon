#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	int failed = 0;
	if (argc < 2) {
		u_puts("usage: mkdir directory ...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		if (u_mkdir(argv[i], 0777) < 0) {
			u_puts("mkdir: cannot create: ");
			u_puts(argv[i]);
			u_puts("\n");
			failed = 1;
		}
	}
	return failed;
}
