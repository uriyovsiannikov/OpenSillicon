#include "../include/os.h"

#define BUFFER_SIZE 512U
#define TAIL_CAPACITY (1024U * 1024U)
#define DEFAULT_LINES 10U

static char saved[TAIL_CAPACITY];

static int
parse_count(const char *text, uint64_t *count)
{
	uint64_t value = 0;
	if (*text == '\0')
		return 0;
	while (*text != '\0') {
		uint64_t digit;
		if (*text < '0' || *text > '9')
			return 0;
		digit = (uint64_t)(*text++ - '0');
		if (value > (UINT64_MAX - digit) / 10)
			return 0;
		value = value * 10 + digit;
	}
	*count = value;
	return 1;
}

static int
write_all(const char *data, size_t length)
{
	while (length != 0) {
		long amount = u_write(1, data, length);
		if (amount <= 0 || (size_t)amount > length)
			return 0;
		data += (size_t)amount;
		length -= (size_t)amount;
	}
	return 1;
}

static int
tail_descriptor(uint64_t fd, uint64_t lines)
{
	char input[BUFFER_SIZE];
	size_t used = 0;
	size_t next = 0;
	size_t oldest;
	long amount;
	while ((amount = u_read(fd, input, sizeof(input))) > 0) {
		size_t i;
		for (i = 0; i < (size_t)amount; i++) {
			saved[next] = input[i];
			next = (next + 1) % sizeof(saved);
			if (used < sizeof(saved))
				used++;
		}
	}
	if (amount < 0)
		return 0;
	if (lines == 0)
		return 1;
	{
		uint64_t seen = 0;
		size_t start = used;
		oldest = (next + sizeof(saved) - used) % sizeof(saved);
		if (start != 0 && saved[(oldest + start - 1) % sizeof(saved)] == '\n')
			start--;
		while (start > 0 && seen < lines) {
			start--;
			if (saved[(oldest + start) % sizeof(saved)] == '\n') {
				seen++;
				if (seen == lines) start++;
			}
		}
		while (start < used) {
			char output[BUFFER_SIZE];
			size_t chunk = used - start;
			size_t contiguous = sizeof(saved) -
			    ((oldest + start) % sizeof(saved));
			if (chunk > sizeof(output)) chunk = sizeof(output);
			if (chunk > contiguous) chunk = contiguous;
			for (size_t j = 0; j < chunk; j++)
				output[j] = saved[(oldest + start + j) % sizeof(saved)];
			if (!write_all(output, chunk)) return 0;
			start += chunk;
		}
		return 1;
	}
}

static int
tail_file(const char *name, uint64_t lines)
{
	long fd;
	int result;
	if (u_streq(name, "-"))
		return tail_descriptor(0, lines);
	fd = u_open(name, U_O_RDONLY);
	if (fd < 0) {
		u_puts("tail: cannot open: ");
		u_puts(name);
		u_puts("\n");
		return 0;
	}
	result = tail_descriptor((uint64_t)fd, lines);
	if (u_close((uint64_t)fd) < 0)
		result = 0;
	if (!result) {
		u_puts("tail: read, write, or close failed: ");
		u_puts(name);
		u_puts("\n");
	}
	return result;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t lines = DEFAULT_LINES, first = 1, i;
	int failed = 0;
	if (argc > 1 && u_streq(argv[1], "-n")) {
		if (argc < 3 || !parse_count(argv[2], &lines))
			goto usage;
		first = 3;
	} else if (argc > 1 && argv[1][0] == '-') {
		goto usage;
	}
	if (first == argc)
		return tail_descriptor(0, lines) ? 0 : 1;
	for (i = first; i < argc; i++)
		if (!tail_file(argv[i], lines))
			failed = 1;
	return failed;
usage:
	u_puts("usage: tail [-n lines] [file ...]\n");
	return 1;
}
