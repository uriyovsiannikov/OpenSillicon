#include "../include/os.h"

#define MAX_VALUE 1000000000000ULL

static int
parse_value(const char *text, uint64_t *value)
{
	uint64_t parsed = 0;
	if (*text == '\0')
		return 0;
	while (*text != '\0') {
		uint64_t digit;
		if (*text < '0' || *text > '9')
			return 0;
		digit = (uint64_t)(*text++ - '0');
		if (parsed > (MAX_VALUE - digit) / 10)
			return 0;
		parsed = parsed * 10 + digit;
	}
	*value = parsed;
	return 1;
}

static void
print_value(uint64_t value)
{
	char digits[20];
	size_t length = 0;
	do {
		digits[length++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0);
	while (length != 0)
		u_putn(&digits[--length], 1);
	u_puts("\n");
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t first = 1, step = 1, last, value;
	if (argc == 2) {
		if (!parse_value(argv[1], &last))
			goto usage;
	} else if (argc == 3) {
		if (!parse_value(argv[1], &first) ||
		    !parse_value(argv[2], &last))
			goto usage;
	} else if (argc == 4) {
		if (!parse_value(argv[1], &first) ||
		    !parse_value(argv[2], &step) || step == 0 ||
		    !parse_value(argv[3], &last))
			goto usage;
	} else {
		goto usage;
	}
	if (first > last)
		return 0;
	for (value = first;;) {
		print_value(value);
		if (last - value < step)
			break;
		value += step;
	}
	return 0;
usage:
	u_puts("usage: seq [first [step]] last\n");
	return 1;
}
