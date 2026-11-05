#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	int system_name = 0;
	int machine = 0;
	uint64_t i;
	int emitted = 0;

	if (argc == 1)
		system_name = 1;
	for (i = 1; i < argc; i++) {
		const char *option = argv[i];
		size_t j;
		if (option[0] != '-' || option[1] == '\0') {
			u_puts("uname: unsupported operand: ");
			u_puts(option);
			u_puts("\n");
			return 1;
		}
		for (j = 1; option[j] != '\0'; j++) {
			if (option[j] == 'a') {
				system_name = 1;
				machine = 1;
			} else if (option[j] == 's')
				system_name = 1;
			else if (option[j] == 'm')
				machine = 1;
			else {
				u_puts("uname: unsupported option: -");
				u_putn(&option[j], 1);
				u_puts("\n");
				return 1;
			}
		}
	}
	if (!system_name && !machine)
		system_name = 1;
	if (system_name) {
		u_puts("OpenSillicon");
		emitted = 1;
	}
	if (machine) {
		if (emitted)
			u_puts(" ");
		u_puts("x86_64");
	}
	u_puts("\n");
	return 0;
}
