#include "include/os.h"

#define LINE_CAPACITY 256U
#define ARGUMENT_CAPACITY 16U
#define PATH_CAPACITY 256U

static char current_directory[PATH_CAPACITY] = "/";

static int
resolve(const char *path, char *absolute)
{
	return u_normalize_path(current_directory, path, absolute, PATH_CAPACITY);
}

static void
print_error(const char *operation, const char *path)
{
	u_puts("sh: ");
	u_puts(operation);
	u_puts(": ");
	u_puts(path);
	u_puts(" failed\n");
}

static void
command_ls(const char *operand)
{
	char path[PATH_CAPACITY];
	struct u_dirent entry;
	long descriptor;
	long result;

	if (!resolve(operand == 0 ? "." : operand, path)) {
		print_error("ls", operand == 0 ? "." : operand);
		return;
	}
	descriptor = u_open(path, U_O_RDONLY | U_O_DIRECTORY);
	if (descriptor < 0) {
		print_error("ls", path);
		return;
	}
	while ((result = u_readdir((uint64_t)descriptor, &entry)) > 0) {
		u_puts(entry.name);
		u_puts("\n");
	}
	if (result < 0)
		print_error("ls", path);
	(void)u_close((uint64_t)descriptor);
}

static void
command_cat(const char *operand)
{
	char path[PATH_CAPACITY];
	char buffer[256];
	long descriptor;
	long amount;

	if (operand == 0 || !resolve(operand, path)) {
		print_error("cat", operand == 0 ? "(missing path)" : operand);
		return;
	}
	descriptor = u_open(path, U_O_RDONLY);
	if (descriptor < 0) {
		print_error("cat", path);
		return;
	}
	while ((amount = u_read((uint64_t)descriptor, buffer,
	    sizeof(buffer))) > 0)
		u_putn(buffer, (size_t)amount);
	if (amount < 0)
		print_error("cat", path);
	(void)u_close((uint64_t)descriptor);
}

static void
command_external(const char *command)
{
	char path[PATH_CAPACITY];
	long child;
	uint8_t status = 0xff;

	if (command[0] == '/') {
		if (u_strlen(command) >= sizeof(path)) {
			print_error("exec", command);
			return;
		}
		for (size_t i = 0; command[i] != '\0'; i++)
			path[i] = command[i];
		path[u_strlen(command)] = '\0';
	} else {
		if (u_strlen(command) + 6 >= sizeof(path)) {
			print_error("exec", command);
			return;
		}
		path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n';
		path[4] = '/';
		for (size_t i = 0; command[i] != '\0'; i++)
			path[5 + i] = command[i];
		path[5 + u_strlen(command)] = '\0';
	}
	child = u_fork();
	if (child < 0) {
		print_error("fork", command);
		return;
	}
	if (child == 0) {
		(void)u_exec_path(path);
		print_error("exec", path);
		u_exit(127);
	}
	if (u_waitpid((uint64_t)child, &status) < 0)
		print_error("waitpid", command);
}

static int
run_command(char *line)
{
	char *arguments[ARGUMENT_CAPACITY];
	char *cursor = line;
	size_t count = 0;

	while (*cursor != '\0') {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
		    *cursor == '\n') cursor++;
		if (*cursor == '\0') break;
		if (count == ARGUMENT_CAPACITY) {
			u_puts("sh: too many arguments\n");
			return 1;
		}
		arguments[count++] = cursor;
		while (*cursor != '\0' && *cursor != ' ' && *cursor != '\t' &&
		    *cursor != '\r' && *cursor != '\n') cursor++;
		if (*cursor != '\0') *cursor++ = '\0';
	}
	if (count == 0)
		return 1;
	if (u_streq(arguments[0], "help")) {
		u_puts("help echo pwd cd ls cat exit\n");
	} else if (u_streq(arguments[0], "echo")) {
		for (size_t i = 1; i < count; i++) {
			if (i != 1) u_puts(" ");
			u_puts(arguments[i]);
		}
		u_puts("\n");
	} else if (u_streq(arguments[0], "pwd")) {
		u_puts(current_directory);
		u_puts("\n");
	} else if (u_streq(arguments[0], "cd")) {
		char path[PATH_CAPACITY];
		struct u_stat status;
		const char *destination = count > 1 ? arguments[1] : "/";

		if (!resolve(destination, path) || u_stat_path(path, &status) < 0 ||
		    status.type != 2) {
			print_error("cd", destination);
		} else {
			for (size_t i = 0; path[i] != '\0'; i++)
				current_directory[i] = path[i];
			current_directory[u_strlen(path)] = '\0';
		}
	} else if (u_streq(arguments[0], "ls")) {
		command_ls(count > 1 ? arguments[1] : 0);
	} else if (u_streq(arguments[0], "cat")) {
		command_cat(count > 1 ? arguments[1] : 0);
	} else if (u_streq(arguments[0], "exit")) {
		return 0;
	} else {
		command_external(arguments[0]);
	}
	return 1;
}

int
user_main(void)
{
	char line[LINE_CAPACITY];
	u_puts("OpenSillicon shell ready\n");
	for (;;) {
		long amount;
		u_puts(current_directory);
		u_puts("# ");
		amount = u_read(0, line, sizeof(line) - 1);
		if (amount == 0) {
			u_puts("\n");
			u_exit(0);
		}
		if (amount < 0) {
			if (amount == -19) {
				(void)u_sleep_ticks(100);
				continue;
			}
			print_error("read", "stdin");
			continue;
		}
		line[amount] = '\0';
		if (!run_command(line))
			u_exit(0);
	}
}
