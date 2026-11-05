#include "../include/os.h"

static int
write_all(const char *text, size_t length)
{
	while (length != 0) {
		long amount = u_write(1, text, length);
		if (amount <= 0 || (size_t)amount > length)
			return 0;
		text += (size_t)amount;
		length -= (size_t)amount;
	}
	return 1;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	for (i = 1; i < argc; i++) {
		if (i != 1 && !write_all(" ", 1))
			return 1;
		if (!write_all(argv[i], u_strlen(argv[i])))
			return 1;
	}
	return write_all("\n", 1) ? 0 : 1;
}
