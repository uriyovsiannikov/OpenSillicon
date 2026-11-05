#include "../include/os.h"

#define PATH_SIZE 256U

static int
list_path(const char *argument)
{
	char path[PATH_SIZE];
	struct u_stat status;
	struct u_dirent entry;
	long descriptor;
	long result;
	long close_result;

	if (u_strlen(argument) >= sizeof(path)) {
		u_puts("ls: path too long: ");
		u_puts(argument);
		u_puts("\n");
		return 1;
	}
	for (size_t i = 0; argument[i] != '\0'; i++)
		path[i] = argument[i];
	path[u_strlen(argument)] = '\0';
	if (u_stat_path(path, &status) < 0) {
		u_puts("ls: cannot access: ");
		u_puts(argument);
		u_puts("\n");
		return 1;
	}
	if (status.type != 2) {
		u_puts(argument);
		u_puts("\n");
		return 0;
	}
	descriptor = u_open(path, U_O_RDONLY | U_O_DIRECTORY);
	if (descriptor < 0) {
		u_puts("ls: cannot open directory: ");
		u_puts(argument);
		u_puts("\n");
		return 1;
	}
	while ((result = u_readdir((uint64_t)descriptor, &entry)) > 0) {
		if (u_streq(entry.name, ".") || u_streq(entry.name, ".."))
			continue;
		u_puts(entry.name);
		u_puts("\n");
	}
	close_result = u_close((uint64_t)descriptor);
	if (result < 0) {
		u_puts("ls: directory read failed: ");
		u_puts(argument);
		u_puts("\n");
		return 1;
	}
	if (close_result < 0) {
		u_puts("ls: close failed: ");
		u_puts(argument);
		u_puts("\n");
		return 1;
	}
	return 0;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t index;
	int failed = 0;

	if (argc == 1)
		return list_path(".");
	for (index = 1; index < argc; index++)
		if (list_path(argv[index]))
			failed = 1;
	return failed;
}
