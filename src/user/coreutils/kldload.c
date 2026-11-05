#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	long result;
	if (argc != 2) {
		u_puts("usage: kldload module.ko\n");
		return 2;
	}
	result = u_kldload(argv[1]);
	if (result < 0) {
		u_puts("kldload: load failed (error ");
		u_put_u64((uint64_t)-result);
		u_puts(")\n");
		return 1;
	}
	return 0;
}
