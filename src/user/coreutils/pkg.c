#include <stddef.h>

#include "../include/os.h"

#define PKG_DB "/var/db/pkg"

static int
show_info(const char *name)
{
	char path[256];
	char buffer[512];
	size_t length = u_strlen(name), used = 0;
	long fd, amount;
	if (length == 0 || length > 238)
		return 1;
	for (size_t i = 0; i < length; i++) {
		char c = name[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '+' ||
		    c == '-'))
			return 1;
	}
	for (size_t i = 0; i < sizeof(PKG_DB) - 1; i++) path[i] = PKG_DB[i];
	path[sizeof(PKG_DB) - 1] = '/';
	for (size_t i = 0; i < length; i++) path[sizeof(PKG_DB) + i] = name[i];
	if (length < 5 || name[length - 5] != '.' || name[length - 4] != 'j' ||
	    name[length - 3] != 's' || name[length - 2] != 'o' ||
	    name[length - 1] != 'n') {
		path[sizeof(PKG_DB) + length] = '.';
		path[sizeof(PKG_DB) + length + 1] = 'j';
		path[sizeof(PKG_DB) + length + 2] = 's';
		path[sizeof(PKG_DB) + length + 3] = 'o';
		path[sizeof(PKG_DB) + length + 4] = 'n';
		length += 5;
	}
	path[sizeof(PKG_DB) + length] = '\0';
	fd = u_open(path, U_O_RDONLY);
	if (fd < 0) {
		u_puts("pkg: package not installed: "); u_puts(name); u_puts("\n");
		return 1;
	}
	while ((amount = u_read((uint64_t)fd, buffer, sizeof(buffer))) > 0) {
		used += (size_t)amount;
		if (used > 65536 || u_write(1, buffer, (size_t)amount) != amount) {
			(void)u_close((uint64_t)fd);
			u_puts("pkg: metadata too large or output failed\n");
			return 1;
		}
	}
	(void)u_close((uint64_t)fd);
	return amount < 0;
}

int
user_main(uint64_t argc, char **argv)
{
	long fd;
	struct u_dirent entry;
	long result;
	if (argc == 1 || (argc == 2 && u_streq(argv[1], "list"))) {
		fd = u_open(PKG_DB, U_O_RDONLY | U_O_DIRECTORY);
		if (fd < 0) {
			u_puts("pkg: package database is unavailable\n");
			return 1;
		}
		while ((result = u_readdir((uint64_t)fd, &entry)) > 0) {
			if (entry.type == 1 && entry.name[0] != '.') {
				u_putn(entry.name, u_strlen(entry.name));
				u_puts("\n");
			}
		}
		(void)u_close((uint64_t)fd);
		return result < 0;
	}
	if (argc == 3 && u_streq(argv[1], "info"))
		return show_info(argv[2]);
	u_puts("usage: pkg [list | info package]\n");
	u_puts("Package install and repository commands are not available yet.\n");
	return 2;
}
