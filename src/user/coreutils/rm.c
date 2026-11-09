#include "../include/os.h"

#define PATH_SIZE 256U

static int
remove_path(const char *path, int recursive, int force)
{
	struct u_stat status;
	if (u_stat_path(path, &status) < 0) {
		if (force) return 0;
		u_puts("rm: cannot access: "); u_puts(path); u_puts("\n");
		return 1;
	}
	if (status.type == 2 && recursive) {
		struct u_dirent entry;
		long fd = u_open(path, U_O_RDONLY | U_O_DIRECTORY);
		int failed = 0;
		if (fd < 0) {
			u_puts("rm: cannot open directory: "); u_puts(path); u_puts("\n");
			return 1;
		}
		long read_result;
		while ((read_result = u_readdir((uint64_t)fd, &entry)) > 0) {
			char child[PATH_SIZE];
			size_t p = u_strlen(path), n = u_strlen(entry.name), j;
			if (u_streq(entry.name, ".") || u_streq(entry.name, "..")) continue;
			if (p + n + 2 > sizeof(child)) { failed = 1; continue; }
			for (j = 0; j < p; j++) child[j] = path[j];
			if (p != 0 && child[p - 1] != '/') child[p++] = '/';
			for (j = 0; j < n; j++) child[p + j] = entry.name[j];
			child[p + n] = '\0';
			if (remove_path(child, recursive, force)) failed = 1;
		}
		if (read_result < 0) failed = 1;
		if (u_close((uint64_t)fd) < 0) failed = 1;
		if (failed) return 1;
	}
	if (u_unlink(path, status.type == 2) < 0) {
		u_puts("rm: cannot remove: "); u_puts(path); u_puts("\n");
		return 1;
	}
	return 0;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t i = 1;
	int recursive = 0, force = 0, failed = 0;
	while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
		if (u_streq(argv[i], "--")) { i++; break; }
		for (size_t j = 1; argv[i][j] != '\0'; j++) {
			if (argv[i][j] == 'r' || argv[i][j] == 'R') recursive = 1;
			else if (argv[i][j] == 'f') force = 1;
			else { u_puts("usage: rm [-rf] [--] path ...\n"); return 1; }
		}
		i++;
	}
	if (i == argc) {
		if (force) return 0;
		u_puts("usage: rm [-rf] [--] path ...\n"); return 1;
	}
	for (; i < argc; i++)
		if (remove_path(argv[i], recursive, force)) failed = 1;
	return failed;
}
