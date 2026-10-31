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

struct info_line {
	const char *label;
	const char *value;
};

static const struct info_line information[] = {
	{ "OS: ", "OpenSillicon" },
	{ "Kernel: ", "x86_64" },
	{ "Architecture: ", "x86_64" },
	{ "Shell: ", "/bin/sh" },
	{ "Userland: ", "C11" },
	{ "VFS: ", "SilliconFS" },
	{ "PID: ", NULL }
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
		if ((index != 0 && !write_all(" ", 1)) ||
		    !write_string(colors[index]))
			return 0;
	}
	return 1;
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
		if (row < sizeof(information) / sizeof(information[0])) {
			long pid = -1;

			if (!write_string("\033[96m") ||
			    !write_string(information[row].label) ||
			    !write_string("\033[0m\033[97m"))
				return 1;
			if (information[row].value != NULL) {
				if (!write_string(information[row].value))
					return 1;
			} else {
				pid = u_getpid();
				if (pid < 0)
					return 1;
				u_put_u64((uint64_t)pid);
			}
			if (!write_string("\033[0m"))
				return 1;
		} else if (row == sizeof(information) / sizeof(information[0])) {
			if (!write_palette(normal_palette))
				return 1;
		} else if (row == sizeof(information) / sizeof(information[0]) + 1) {
			if (!write_palette(bright_palette))
				return 1;
		}
		if (!write_all("\n", 1))
			return 1;
	}
	return 0;
}
