#include "include/os.h"

#define LOGO_WIDTH 24U

static const char *const logo[] = {
	"          __",
	"    .-'\" (M /--.",
	"  .'   .'|o|  @ `..",
	" /  .-/       _....'",
	"|  '-/      .' |",
	"|   /      |_\\ |",
	" \\  |    /\\|  /",
	"  '.|   /   .'",
	"    |  /_.-'",
	"    | /",
	"    | |",
	"   (,-,)"
};

#define INFO_LINE_COUNT 8U

static const char *const information_labels[INFO_LINE_COUNT] = {
	"OS: ", "Uptime: ", "Architecture: ", "Shell: ",
	"Memory: ", "/bin entries: ", "Terminal: ", "PID: "
};

static const char *const information_values[4] = {
	"OpenSillicon", "", "x86_64", "/bin/sh"
};

static const char *const logo_colors[] = {
	"\033[94m", "\033[96m", "\033[95m", "\033[94m",
	"\033[96m", "\033[95m", "\033[94m", "\033[96m",
	"\033[95m", "\033[94m", "\033[96m", "\033[95m"
};

static const char *const normal_palette[] = {
	"\033[40m  \033[0m", "\033[41m  \033[0m",
	"\033[42m  \033[0m", "\033[43m  \033[0m",
	"\033[44m  \033[0m", "\033[45m  \033[0m",
	"\033[46m  \033[0m", "\033[47m  \033[0m"
};

static const char *const bright_palette[] = {
	"\033[100m  \033[0m", "\033[101m  \033[0m",
	"\033[102m  \033[0m", "\033[103m  \033[0m",
	"\033[104m  \033[0m", "\033[105m  \033[0m",
	"\033[106m  \033[0m", "\033[107m  \033[0m"
};

static int
write_all(const char *text, size_t length)
{
	while (length != 0) {
		long amount = u_write(1, text, length);

		if (amount <= 0 || (size_t)amount > length)
			return 0;
		text += (size_t)amount;
		length -= (size_t)amount;
	}
	return 1;
}

static int
write_string(const char *text)
{
	return write_all(text, u_strlen(text));
}

static int
write_palette(const char *const colors[8])
{
	size_t index;

	for (index = 0; index < 8; index++) {
		if (!write_string(colors[index]))
			return 0;
	}
	return 1;
}

static int
read_proc(const char *path, char *buffer, size_t capacity)
{
	long descriptor = u_open(path, U_O_RDONLY);
	size_t used = 0;
	long amount;

	if (descriptor < 0)
		return 0;
	while (used < capacity - 1) {
		amount = u_read((uint64_t)descriptor, buffer + used,
		    capacity - 1 - used);
		if (amount == -4)
			continue;
		if (amount < 0) {
			(void)u_close((uint64_t)descriptor);
			return 0;
		}
		if (amount == 0)
			break;
		used += (size_t)amount;
	}
	(void)u_close((uint64_t)descriptor);
	buffer[used] = '\0';
	return used != 0 && used < capacity - 1;
}

static int
parse_u64(const char **text, uint64_t *value)
{
	const char *cursor = *text;
	uint64_t result = 0;

	if (*cursor < '0' || *cursor > '9')
		return 0;
	while (*cursor >= '0' && *cursor <= '9') {
		uint64_t digit = (uint64_t)(*cursor++ - '0');
		if (result > (UINT64_MAX - digit) / 10U)
			return 0;
		result = result * 10U + digit;
	}
	*text = cursor;
	*value = result;
	return 1;
}

static int
write_uptime(void)
{
	char content[128];
	const char *cursor = content;
	uint64_t seconds;

	if (!read_proc("/proc/uptime", content, sizeof(content)) ||
	    !parse_u64(&cursor, &seconds) || *cursor != '.')
		return write_string("unavailable");
	u_put_u64(seconds / 3600U);
	if (!write_string("h "))
		return 0;
	u_put_u64(seconds / 60U % 60U);
	if (!write_string("m "))
		return 0;
	u_put_u64(seconds % 60U);
	return write_string("s");
}

static int
memory_value(const char *content, const char *label, uint64_t *value)
{
	size_t length = u_strlen(label);

	while (*content != '\0') {
		size_t i = 0;
		while (i < length && content[i] == label[i])
			i++;
		if (i == length) {
			content += length;
			while (*content == ' ' || *content == '\t')
				content++;
			return parse_u64(&content, value) &&
			    content[0] == ' ' && content[1] == 'k' &&
			    content[2] == 'B' && content[3] == '\n';
		}
		while (*content != '\0' && *content != '\n')
			content++;
		if (*content == '\n')
			content++;
	}
	return 0;
}

static int
write_mib(uint64_t kib)
{
	u_put_u64(kib / 1024U);
	if (!write_string("."))
		return 0;
	u_put_u64(kib % 1024U * 10U / 1024U);
	return 1;
}

static int
write_memory(void)
{
	char content[128];
	uint64_t total, free;

	if (!read_proc("/proc/meminfo", content, sizeof(content)) ||
	    !memory_value(content, "MemTotal:", &total) ||
	    !memory_value(content, "MemFree:", &free) || free > total)
		return write_string("unavailable");
	return write_mib(total - free) && write_string(" / ") &&
	    write_mib(total) && write_string(" MiB");
}

static int
write_dynamic_information(size_t row)
{
	if (row == 1)
		return write_uptime();
	if (row < 4)
		return write_string(information_values[row]);
	if (row == 4)
		return write_memory();
	if (row == 5) {
		struct u_dirent entry;
		long descriptor = u_open("/bin", U_O_RDONLY | U_O_DIRECTORY);
		long result;
		uint64_t count = 0;
		if (descriptor < 0)
			return write_string("unavailable");
		while ((result = u_readdir((uint64_t)descriptor, &entry)) > 0) {
			if (!u_streq(entry.name, ".") && !u_streq(entry.name, ".."))
				count++;
		}
		(void)u_close((uint64_t)descriptor);
		if (result < 0)
			return write_string("unavailable");
		u_put_u64(count);
		return 1;
	}
	if (row == 6) {
		struct u_winsize size;
		if (u_ioctl(1, 0x5413U, &size) != 0)
			return write_string("unavailable");
		u_put_u64(size.rows);
		if (!write_all("x", 1))
			return 0;
		u_put_u64(size.columns);
		return 1;
	}
	{
		long pid = u_getpid();
		if (pid < 0)
			return write_string("unavailable");
		u_put_u64((uint64_t)pid);
		return 1;
	}
}

int
user_main(uint64_t argc, char **argv)
{
	size_t row;
	(void)argv;
	if (argc != 1) {
		u_puts("fetch: arguments are unsupported\n");
		return 1;
	}

	for (row = 0; row < sizeof(logo) / sizeof(logo[0]); row++) {
		size_t length = u_strlen(logo[row]);
		size_t column;

		if (!write_string(logo_colors[row]) ||
		    !write_all(logo[row], length) || !write_string("\033[0m"))
			return 1;
		for (column = length; column < LOGO_WIDTH; column++) {
			if (!write_all(" ", 1))
				return 1;
		}
		if (row < INFO_LINE_COUNT) {
			if (!write_string("\033[96m") ||
			    !write_string(information_labels[row]) ||
			    !write_string("\033[0m"))
				return 1;
			if (!write_dynamic_information(row) || !write_string("\033[0m"))
				return 1;
		} else if (row == INFO_LINE_COUNT + 1) {
			if (!write_palette(normal_palette))
				return 1;
		} else if (row == INFO_LINE_COUNT + 2) {
			if (!write_palette(bright_palette))
				return 1;
		}
		if (!write_all("\n", 1))
			return 1;
	}
	return 0;
}
