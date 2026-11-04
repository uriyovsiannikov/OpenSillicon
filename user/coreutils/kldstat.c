#include <stddef.h>

#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	char buffer[256];
	long fd;
	long amount;
	(void)argv;
	if (argc != 1) {
		static const char usage[] = "usage: kldstat\n";
		(void)u_write(2, usage, sizeof(usage) - 1U);
		return 2;
	}
	fd = u_open("/proc/modules", U_O_RDONLY);
	if (fd < 0) {
		static const char message[] = "kldstat: cannot read /proc/modules\n";
		(void)u_write(2, message, sizeof(message) - 1U);
		return 1;
	}
	while ((amount = u_read((uint64_t)fd, buffer, sizeof(buffer))) > 0) {
		long written = u_write(1, buffer, (size_t)amount);
		if (written != amount) {
			(void)u_close((uint64_t)fd);
			return 1;
		}
	}
	(void)u_close((uint64_t)fd);
	return amount < 0 ? 1 : 0;
}
