#include "include/os.h"

struct shell_startup_phdr {
	uint32_t type;
	uint32_t flags;
	uint64_t offset;
	uint64_t virtual_address;
	uint64_t physical_address;
	uint64_t file_size;
	uint64_t memory_size;
	uint64_t alignment;
};

static __thread volatile uint64_t shell_tls_value =
	UINT64_C(0x5348454c4c544c53);
static uint64_t shell_tls_storage[128] __attribute__((aligned(16)));

#define LINE_CAPACITY 256U
#define ARGUMENT_CAPACITY U_EXEC_ARG_MAX
#define PATH_CAPACITY 256U

static int
initialize_shell_tls(const uint64_t *startup_stack)
{
	const struct shell_startup_phdr *headers =
	    (const struct shell_startup_phdr *)(uintptr_t)startup_stack[4];
	const struct shell_startup_phdr *tls = 0;
	uintptr_t thread_pointer = (uintptr_t)&shell_tls_storage[64];
	uint8_t *destination;
	const uint8_t *source;
	uint64_t i;

	for (i = 0; i < startup_stack[8]; i++) {
		if (headers[i].type == 7) {
			tls = &headers[i];
			break;
		}
	}
	if (tls == 0 || tls->file_size > tls->memory_size ||
	    tls->memory_size > thread_pointer -
	    (uintptr_t)&shell_tls_storage[0] || tls->alignment == 0 ||
	    (tls->alignment & (tls->alignment - 1)) != 0 ||
	    (thread_pointer & (tls->alignment - 1)) != 0)
		return 0;
	destination = (uint8_t *)(thread_pointer - tls->memory_size);
	source = (const uint8_t *)(uintptr_t)tls->virtual_address;
	for (i = 0; i < tls->memory_size; i++)
		destination[i] = 0;
	for (i = 0; i < tls->file_size; i++)
		destination[i] = source[i];
	*(uintptr_t *)thread_pointer = thread_pointer;
	return u_set_fs_base(thread_pointer) == 0 &&
	    shell_tls_value == UINT64_C(0x5348454c4c544c53);
}

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
command_external(size_t argc, const char *const argv[])
{
	char path[PATH_CAPACITY];
	const char *command = argv[0];
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
		(void)u_exec_path_argv(path, argc, argv);
		print_error("exec", path);
		u_exit(127);
	}
	if (u_waitpid((uint64_t)child, &status) < 0)
		print_error("waitpid", command);
	else if (status != 0) {
		u_puts("sh: command exited status ");
		u_put_u64(status);
		u_puts("\n");
	}
}

static int
run_command(char *line)
{
	char *arguments[ARGUMENT_CAPACITY + 1];
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
		u_puts("help cd exit; external commands: ls cat echo pwd true false clear uname fetch\n");
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
	} else if (u_streq(arguments[0], "exit")) {
		return 0;
	} else {
		arguments[count] = 0;
		command_external(count, (const char *const *)arguments);
	}
	return 1;
}

int
user_main(uint64_t argc, char **argv, const uint64_t *startup_stack)
{
	char line[LINE_CAPACITY];
	const struct shell_startup_phdr *phdr;
	if (argc != 0 || argv == NULL || argv[0] != NULL ||
	    startup_stack == 0 || ((uintptr_t)startup_stack & 15U) != 0 ||
	    startup_stack[0] != 0 || startup_stack[1] != 0 ||
	    startup_stack[2] != 0 || startup_stack[3] != 3 ||
	    startup_stack[5] != 4 ||
	    startup_stack[6] != sizeof(*phdr) || startup_stack[7] != 5 ||
	    startup_stack[9] != 6 || startup_stack[10] != 4096 ||
	    startup_stack[11] != 9 || startup_stack[13] != 7 ||
	    startup_stack[14] != 0 || startup_stack[15] != 0 ||
	    startup_stack[16] != 0 || startup_stack[8] == 0 ||
	    startup_stack[4] == 0) {
		u_puts("OpenSillicon shell: malformed exec startup stack\n");
		u_exit(1);
	}
	if (!initialize_shell_tls(startup_stack)) {
		u_puts("OpenSillicon shell: exec PT_TLS setup failed\n");
		u_exit(1);
	}
	phdr = (const struct shell_startup_phdr *)(uintptr_t)startup_stack[4];
	if (phdr[0].type != 1 || (phdr[0].flags & 4U) == 0 ||
	    phdr[0].offset != 0 || phdr[0].virtual_address + 64 !=
	    startup_stack[4] || phdr[0].file_size <
	    64 + startup_stack[8] * sizeof(*phdr)) {
		u_puts("OpenSillicon shell: unreadable AT_PHDR\n");
		u_exit(1);
	}
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
