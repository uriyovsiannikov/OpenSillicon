#include "../include/os.h"

int
user_main(uint64_t argc, char **argv)
{
	const char *path;
	size_t length;
	size_t begin;
	size_t end;
	if (argc < 2 || argc > 3) {
		u_puts("usage: basename path [suffix]\n");
		return 1;
	}
	path = argv[1];
	length = u_strlen(path);
	while (length > 1 && path[length - 1] == '/')
		length--;
	begin = length;
	while (begin > 0 && path[begin - 1] != '/')
		begin--;
	if (length == 0) {
		u_puts(".\n");
		return 0;
	}
	end = length;
	if (argc == 3) {
		size_t suffix_length = u_strlen(argv[2]);
		if (suffix_length < end - begin && suffix_length != 0) {
			size_t i;
			for (i = 0; i < suffix_length; i++)
				if (path[end - suffix_length + i] != argv[2][i])
					break;
			if (i == suffix_length)
				end -= suffix_length;
		}
	}
	if (end == begin && path[0] == '/')
		u_puts("/\n");
	else {
		u_putn(path + begin, end - begin);
		u_puts("\n");
	}
	return 0;
}
