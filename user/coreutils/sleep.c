#include "../include/os.h"

static int
parse_seconds(const char *text, uint64_t *seconds)
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
	*seconds = value;
	return 1;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t seconds;
	uint64_t ticks;
	long result;
	if (argc != 2 || !parse_seconds(argv[1], &seconds) ||
	    seconds > UINT64_MAX / U_TIMER_HZ) {
		u_puts("usage: sleep whole-seconds\n");
		return 1;
	}
	ticks = seconds * U_TIMER_HZ;
	result = u_sleep_ticks(ticks);
	if (result < 0) {
		u_puts("sleep: interrupted or invalid duration\n");
		return 1;
	}
	return 0;
}
