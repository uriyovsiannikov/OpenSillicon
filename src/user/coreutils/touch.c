#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	int failed = 0;
	if (argc < 2) {
		u_puts("usage: touch file ...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		long fd = u_open_mode(argv[i], U_O_WRONLY | U_O_CREAT, 0666);
		if (fd < 0) {
			u_puts("touch: cannot create: "); u_puts(argv[i]); u_puts("\n");
			failed = 1;
		} else if (u_close((uint64_t)fd) < 0) {
			u_puts("touch: close failed: "); u_puts(argv[i]); u_puts("\n");
			failed = 1;
		}
	}
	return failed;
}
