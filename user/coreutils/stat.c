#include "../include/os.h"

#define PATH_SIZE 256U

static const char *
type_name(uint32_t type)
{
	if (type == 1)
		return "regular file";
	if (type == 2)
		return "directory";
	return "unknown";
}

static void
print_octal(uint32_t value)
{
	char digits[11];
	size_t count = 0;
	do {
		digits[count++] = (char)('0' + (value & 7U));
		value >>= 3;
	} while (value != 0 && count < sizeof(digits));
	while (count != 0)
		u_putn(&digits[--count], 1);
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	int failed = 0;
	if (argc == 1) {
		u_puts("usage: stat file ...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		char path[PATH_SIZE];
		struct u_stat status;
		if (!u_normalize_path("/", argv[i], path, sizeof(path)) ||
		    u_stat_path(path, &status) < 0) {
			u_puts("stat: cannot stat: ");
			u_puts(argv[i]);
			u_puts("\n");
			failed = 1;
			continue;
		}
		u_puts("  File: ");
		u_puts(argv[i]);
		u_puts("\n  Type: ");
		u_puts(type_name(status.type));
		u_puts("\n  Size: ");
		u_put_u64(status.size);
		u_puts(" bytes\n  Mode: ");
		print_octal(status.mode);
		u_puts("\n");
	}
	return failed;
}
