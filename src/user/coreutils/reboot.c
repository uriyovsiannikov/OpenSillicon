#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	long result;
	(void)argv;
	if (argc != 1) {
		u_puts("usage: reboot\n");
		return 2;
	}
	result = u_power_control(U_POWER_CONTROL_REBOOT);
	if (result < 0) {
		u_puts("reboot: system reset failed (error ");
		u_put_u64((uint64_t)-result);
		u_puts(")\n");
		return 1;
	}
	return 0;
}
