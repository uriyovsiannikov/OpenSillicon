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
	"OS: ", "Kernel: ", "Architecture: ", "Shell: ",
	"Root filesystem: ", "/bin entries: ", "Terminal: ", "PID: "
};

static const char *const information_values[4] = {
	"OpenSillicon", "OpenSillicon", "x86_64", "/bin/sh"
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
write_dynamic_information(size_t row)
{
	if (row < 4)
		return write_string(information_values[row]);
	if (row == 4) {
		struct u_stat status;
		return write_string(u_stat_path("/", &status) == 0 &&
		    status.type == 2 ? "available" : "unavailable");
	}
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
			    !write_string("\033[0m\033[97m"))
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
