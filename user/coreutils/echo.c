#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	for (i = 1; i < argc; i++) {
		if (i != 1 && u_write(1, " ", 1) != 1)
			return 1;
		if (u_write(1, argv[i], u_strlen(argv[i])) < 0)
			return 1;
	}
	return u_write(1, "\n", 1) == 1 ? 0 : 1;
}
