#include "../include/os.h"

#define PATH_SIZE 256U

static int
resolve_command(const char *command, char *path)
{
	size_t i;
	if (command[0] == '/')
		return u_normalize_path("/", command, path, PATH_SIZE);
	for (i = 0; command[i] != '\0'; i++) {
		if (command[i] == '/') {
			char cwd[PATH_SIZE];
			if (u_getcwd(cwd, sizeof(cwd)) < 0)
				return 0;
			return u_normalize_path(cwd, command, path, PATH_SIZE);
		}
	}
	if (u_strlen(command) + 6 >= PATH_SIZE)
		return 0;
	path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n';
	path[4] = '/';
	{
		size_t i;
		for (i = 0; command[i] != '\0'; i++)
			path[5 + i] = command[i];
		path[5 + i] = '\0';
	}
	return 1;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i;
	int missing = 0;
	if (argc < 2) {
		u_puts("usage: which command ...\n");
		return 1;
	}
	for (i = 1; i < argc; i++) {
		char path[PATH_SIZE];
		struct u_stat status;
		if (!resolve_command(argv[i], path) ||
		    u_stat_path(path, &status) < 0 || status.type != 1 ||
		    (status.mode & 0111U) == 0) {
			missing = 1;
			continue;
		}
		u_puts(path);
		u_puts("\n");
	}
	return missing;
}
