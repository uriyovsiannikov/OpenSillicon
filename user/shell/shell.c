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
#define HELP_COMMAND_LIMIT 128U
#define HELP_NAME_CAPACITY 256U
#define HELP_COLUMN_WIDTH 22U
#define HISTORY_CAPACITY 16U

static const char *const internal_commands[] = {
	"help", "cd", "exit", "history"
};

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
static char command_history[HISTORY_CAPACITY][LINE_CAPACITY];
static size_t history_count;
static size_t history_next;
static size_t history_position;
static int cursor_visible = 1;

static int
resolve(const char *path, char *absolute)
{
	return u_normalize_path(current_directory, path, absolute, PATH_CAPACITY);
}

static size_t
history_slot(size_t position)
{
	return (history_next + HISTORY_CAPACITY - history_count + position) %
	    HISTORY_CAPACITY;
}

static int
same_text(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static void
remember_command(const char *line)
{
	size_t i;
	if (line[0] == '\0')
		return;
	if (history_count != 0 && same_text(
	    command_history[history_slot(history_count - 1)], line))
		return;
	for (i = 0; i < LINE_CAPACITY - 1 && line[i] != '\0'; i++)
		command_history[history_next][i] = line[i];
	command_history[history_next][i] = '\0';
	history_next = (history_next + 1) % HISTORY_CAPACITY;
	if (history_count < HISTORY_CAPACITY)
		history_count++;
}

static void
print_history(void)
{
	size_t i;
	for (i = 0; i < history_count; i++) {
		u_put_u64(i + 1);
		u_puts("  ");
		u_puts(command_history[history_slot(i)]);
		u_puts("\n");
	}
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

static int
is_executable_entry(const struct u_dirent *entry)
{
	return entry->type == 1 && (entry->mode & 0111U) != 0 &&
	    !u_streq(entry->name, ".") && !u_streq(entry->name, "..");
}

static long
count_bin_commands(void)
{
	struct u_dirent entry;
	long descriptor = u_open("/bin", U_O_RDONLY | U_O_DIRECTORY);
	long result;
	long count = 0;
	if (descriptor < 0)
		return -1;
	while ((result = u_readdir((uint64_t)descriptor, &entry)) > 0) {
		if (is_executable_entry(&entry)) {
			if (count == HELP_COMMAND_LIMIT) {
				(void)u_close((uint64_t)descriptor);
				return -2;
			}
			count++;
		}
	}
	if (u_close((uint64_t)descriptor) < 0 || result < 0)
		return -1;
	return count;
}

static int
bin_command_at(uint64_t requested, char *name)
{
	struct u_dirent entry;
	long descriptor = u_open("/bin", U_O_RDONLY | U_O_DIRECTORY);
	long result;
	uint64_t index = 0;
	if (descriptor < 0)
		return 0;
	while ((result = u_readdir((uint64_t)descriptor, &entry)) > 0) {
		size_t length;
		if (!is_executable_entry(&entry))
			continue;
		if (index++ != requested)
			continue;
		for (length = 0; length < sizeof(entry.name) &&
		    entry.name[length] != '\0'; length++)
			name[length] = entry.name[length];
		if (length == sizeof(entry.name)) {
			(void)u_close((uint64_t)descriptor);
			return 0;
		}
		name[length] = '\0';
		(void)u_close((uint64_t)descriptor);
		return 1;
	}
	(void)u_close((uint64_t)descriptor);
	return 0;
}

static int
help_command_at(uint64_t requested, uint64_t external_count, char *name)
{
	size_t internal_count = sizeof(internal_commands) /
	    sizeof(internal_commands[0]);
	size_t i;
	if (requested < internal_count) {
		const char *source = internal_commands[requested];
		for (i = 0; source[i] != '\0'; i++)
			name[i] = source[i];
		name[i] = '\0';
		return 1;
	}
	if (requested >= internal_count + external_count)
		return 0;
	return bin_command_at(requested - internal_count, name);
}

static void
print_help_commands(void)
{
	long external_count;
	uint64_t total;
	uint64_t columns = 1;
	uint64_t rows;
	uint64_t row;
	uint64_t max_columns = 4;
	struct u_winsize terminal_size;
	long listing_error = 0;

	external_count = count_bin_commands();
	if (external_count < 0) {
		listing_error = external_count;
		external_count = 0;
	}
	total = sizeof(internal_commands) / sizeof(internal_commands[0]) +
	    (uint64_t)external_count;
	while (columns * columns < total)
		columns++;
	if (u_ioctl(1, 0x5413U, &terminal_size) == 0) {
		max_columns = terminal_size.columns / HELP_COLUMN_WIDTH;
		if (max_columns == 0)
			max_columns = 1;
	}
	if (columns > max_columns)
		columns = max_columns;
	rows = (total + columns - 1) / columns;
	u_puts("Available commands:\n");
	for (row = 0; row < rows; row++) {
		uint64_t column;
		for (column = 0; column < columns; column++) {
			uint64_t index = row * columns + column;
			char name[HELP_NAME_CAPACITY];
			size_t length;
			uint64_t spaces;
			if (index >= total)
				continue;
			if (!help_command_at(index, (uint64_t)external_count, name)) {
				u_puts("sh: cannot read command entry\n");
				return;
			}
			length = u_strlen(name);
			u_puts(name);
			if (column + 1 < columns && index + 1 < total) {
				spaces = length < HELP_COLUMN_WIDTH ?
				    HELP_COLUMN_WIDTH - length : 2;
				while (spaces-- != 0)
					u_puts(" ");
			}
		}
		u_puts("\n");
	}
	if (listing_error != 0)
		u_puts(listing_error == -2 ?
		    "sh: /bin has more than 128 executable entries\n" :
		    "sh: cannot read /bin; listed internal commands only\n");
}

static void
command_external(size_t argc, const char *const argv[])
{
	char path[PATH_CAPACITY];
	const char *command = argv[0];
	long child;
	long wait_result;
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
	if (u_ioctl(0, U_TIOCCANON, 0) < 0) {
		print_error("tty mode", "canonical");
		return;
	}
	child = u_fork();
	if (child < 0) {
		print_error("fork", command);
		goto resume_editor;
	}
	if (child == 0) {
		(void)u_exec_path_argv(path, argc, argv);
		print_error("exec", path);
		u_exit(127);
	}
	do {
		wait_result = u_waitpid((uint64_t)child, &status);
	} while (wait_result == -4);
	if (wait_result < 0)
		print_error("waitpid", command);
	else if (status != 0) {
		u_puts("sh: command exited status ");
		u_put_u64(status);
		u_puts("\n");
	}
resume_editor:
	if (u_ioctl(0, U_TIOCSRAW, 0) < 0) {
		u_puts("sh: cannot restore interactive input mode\n");
		u_exit(1);
	}
}

static void
redraw_line(const char *line, size_t length, size_t cursor)
{
	u_puts("\r");
	u_puts(current_directory);
	u_puts("# ");
	if (cursor_visible) {
		u_putn(line, cursor);
		u_puts("\033[0m_");
		if (cursor < length)
			u_putn(line + cursor + 1, length - cursor - 1);
	} else {
		u_putn(line, length);
	}
	u_puts("\033[K");
	if (cursor_visible && cursor == length) {
		u_puts("\033[1D");
	} else if (cursor < length) {
		u_puts("\033[");
		u_put_u64(length - cursor);
		u_puts("D");
	}
}

static void
load_history_line(char *line, size_t *length, size_t *cursor)
{
	const char *source;
	size_t i;
	if (history_position == history_count) {
		line[0] = '\0';
		*length = 0;
		*cursor = 0;
		return;
	}
	source = command_history[history_slot(history_position)];
	for (i = 0; i < LINE_CAPACITY - 1 && source[i] != '\0'; i++)
		line[i] = source[i];
	line[i] = '\0';
	*length = i;
	*cursor = i;
}

static int
read_command_line(char *line)
{
	size_t length = 0;
	size_t cursor = 0;
	line[0] = '\0';
	history_position = history_count;
	for (;;) {
		char character;
		long amount = u_read(0, &character, 1);
		if (amount == -11) {
			cursor_visible = !cursor_visible;
			redraw_line(line, length, cursor);
			continue;
		}
		if (amount == 0)
			return 0;
		if (amount < 0)
			return (int)amount;
		cursor_visible = 1;
		if (character == '\n') {
			line[length] = '\0';
			/* A blinking cursor is drawn as a literal underscore. Remove it
			 * from the screen before leaving the editor for command output. */
			cursor_visible = 0;
			redraw_line(line, length, cursor);
			u_puts("\n");
			remember_command(line);
			return 1;
		}
		if ((uint8_t)character == 0x1bU) {
			char sequence[2];
			if (u_read(0, sequence, sizeof(sequence)) !=
			    (long)sizeof(sequence) || sequence[0] != '[')
				continue;
			switch (sequence[1]) {
			case 'A':
				if (history_position != 0) {
					history_position--;
					load_history_line(line, &length, &cursor);
					redraw_line(line, length, cursor);
				}
				break;
			case 'B':
				if (history_position < history_count) {
					history_position++;
					load_history_line(line, &length, &cursor);
					redraw_line(line, length, cursor);
				}
				break;
			case 'C':
				if (cursor < length) {
					cursor++;
					redraw_line(line, length, cursor);
				}
				break;
			case 'D':
				if (cursor != 0) {
					cursor--;
					redraw_line(line, length, cursor);
				}
				break;
			case 'H':
				cursor = 0;
				redraw_line(line, length, cursor);
				break;
			case 'F':
				cursor = length;
				redraw_line(line, length, cursor);
				break;
			default:
				break;
			}
			continue;
		}
		if (character == '\t')
			character = ' ';
		if (character == '\b' || (uint8_t)character == 0x7fU) {
			if (cursor != 0) {
				size_t i;
				for (i = cursor; i < length; i++)
					line[i - 1] = line[i];
				length--;
				cursor--;
				line[length] = '\0';
				redraw_line(line, length, cursor);
			}
			continue;
		}
		if ((uint8_t)character == 4U) {
			if (length == 0) {
				u_puts("\n");
				return 0;
			}
			if (cursor < length) {
				size_t i;
				for (i = cursor; i + 1 < length; i++)
					line[i] = line[i + 1];
				length--;
				line[length] = '\0';
				redraw_line(line, length, cursor);
			}
			continue;
		}
		if ((uint8_t)character == 3U) {
			u_puts("^C\n");
			return -2;
		}
		if ((uint8_t)character < 0x20U || (uint8_t)character > 0x7eU ||
		    length + 1 >= LINE_CAPACITY)
			continue;
		{
			size_t i;
			for (i = length; i > cursor; i--)
				line[i] = line[i - 1];
			line[cursor++] = character;
			length++;
			line[length] = '\0';
		}
		cursor_visible = 1;
		redraw_line(line, length, cursor);
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
		print_help_commands();
	} else if (u_streq(arguments[0], "history")) {
		if (count != 1)
			u_puts("usage: history\n");
		else
			print_history();
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
	if (u_ioctl(0, U_TIOCSRAW, 0) < 0) {
		u_puts("OpenSillicon shell: cannot enable interactive input mode\n");
		u_exit(1);
	}
	u_puts("OpenSillicon shell ready\n");
	for (;;) {
		int result;
		u_puts(current_directory);
		u_puts("# ");
		cursor_visible = 1;
		redraw_line(line, 0, 0);
		result = read_command_line(line);
		if (result == 0) {
			(void)u_ioctl(0, U_TIOCCANON, 0);
			u_exit(0);
		}
		if (result < 0) {
			if (result == -2)
				continue;
			if (result == -19) {
				(void)u_sleep_ticks(100);
				continue;
			}
			(void)u_puts("\n");
			print_error("read", "stdin");
			continue;
		}
		if (!run_command(line)) {
			(void)u_ioctl(0, U_TIOCCANON, 0);
			u_exit(0);
		}
	}
}
