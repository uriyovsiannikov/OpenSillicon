#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	const char *path;
	size_t length;
	size_t slash;
	size_t end;
	if (argc != 2) {
		u_puts("usage: dirname path\n");
		return 1;
	}
	path = argv[1];
	length = u_strlen(path);
	while (length > 1 && path[length - 1] == '/')
		length--;
	if (length == 0) {
		u_puts(".\n");
		return 0;
	}
	if (length == 1 && path[0] == '/') {
		u_puts("/\n");
		return 0;
	}
	slash = length;
	while (slash > 0 && path[slash - 1] != '/')
		slash--;
	if (slash == 0)
		u_puts(".\n");
	else if (slash == 1)
		u_puts("/\n");
	else {
		end = slash - 1;
		while (end > 1 && path[end - 1] == '/')
			end--;
		u_putn(path, end);
		u_puts("\n");
	}
	return 0;
}
