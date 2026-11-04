#include "../include/os.h"

#define BUFFER_SIZE 512U
#define PATH_SIZE 256U
#define DEFAULT_LINES 10U
#define MAX_LINES 1000000U

static int
write_all(const char *buffer, size_t length)
{
	while (length != 0) {
		long amount = u_write(1, buffer, length);
		if (amount <= 0 || (size_t)amount > length)
			return 0;
		buffer += (size_t)amount;
		length -= (size_t)amount;
	}
	return 1;
}

static int
parse_count(const char *text, uint64_t *count)
{
	uint64_t value = 0;
	if (*text == '\0')
		return 0;
	while (*text != '\0') {
		if (*text < '0' || *text > '9' ||
		    value > (MAX_LINES - (uint64_t)(*text - '0')) / 10)
			return 0;
		value = value * 10 + (uint64_t)(*text++ - '0');
	}
	*count = value;
	return 1;
}

static int
head_descriptor(uint64_t descriptor, uint64_t limit)
{
	char buffer[BUFFER_SIZE];
	uint64_t lines = 0;
	long amount = 0;

	while (lines < limit && (amount = u_read(descriptor, buffer,
	    sizeof(buffer))) > 0) {
		size_t length = (size_t)amount;
		size_t output = 0;
		while (output < length) {
			if (buffer[output++] == '\n') {
				lines++;
				if (lines == limit)
					break;
			}
		}
		if (!write_all(buffer, output))
			return 0;
	}
	return amount >= 0;
}

static int
head_file(const char *name, uint64_t limit)
{
	long descriptor;
	int result;
	if (u_streq(name, "-"))
		return head_descriptor(0, limit);
	descriptor = u_open(name, U_O_RDONLY);
	if (descriptor < 0) {
		u_puts("head: cannot open: ");
		u_puts(name);
		u_puts("\n");
		return 0;
	}
	result = head_descriptor((uint64_t)descriptor, limit);
	if (u_close((uint64_t)descriptor) < 0)
		result = 0;
	if (!result) {
		u_puts("head: read, write, or close failed: ");
		u_puts(name);
		u_puts("\n");
	}
	return result;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t limit = DEFAULT_LINES;
	uint64_t first_file = 1;
	uint64_t i;
	uint64_t files;
	int failed = 0;
	if (argc > 1 && u_streq(argv[1], "-n")) {
		if (argc < 3 || !parse_count(argv[2], &limit)) {
			u_puts("usage: head [-n lines] [file ...]\n");
			return 1;
		}
		first_file = 3;
	} else if (argc > 1 && argv[1][0] == '-') {
		u_puts("usage: head [-n lines] [file ...]\n");
		return 1;
	}
	files = argc - first_file;
	if (files == 0)
		return head_descriptor(0, limit) ? 0 : 1;
	for (i = first_file; i < argc; i++) {
		if (files > 1) {
			if (i != first_file && !write_all("\n", 1))
				return 1;
			u_puts("==> ");
			u_puts(argv[i]);
			u_puts(" <==\n");
		}
		if (!head_file(argv[i], limit))
			failed = 1;
	}
	return failed;
}
