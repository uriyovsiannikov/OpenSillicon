#include "../include/os.h"

static int
parse_number(const char *text, uint64_t *value)
{
	uint64_t result = 0;
	if (*text == '\0')
		return 0;
	while (*text != '\0') {
		uint64_t digit;
		if (*text < '0' || *text > '9')
			return 0;
		digit = (uint64_t)(*text++ - '0');
		if (result > (UINT64_MAX - digit) / 10)
			return 0;
		result = result * 10 + digit;
	}
	*value = result;
	return 1;
}

static int
parse_signal(const char *text, uint64_t *signo)
{
	if (u_streq(text, "INT") || u_streq(text, "SIGINT"))
		*signo = U_SIGINT;
	else if (u_streq(text, "TERM") || u_streq(text, "SIGTERM"))
		*signo = U_SIGTERM;
	else if (u_streq(text, "KILL") || u_streq(text, "SIGKILL"))
		*signo = U_SIGKILL;
	else if (!parse_number(text, signo))
		return 0;
	return *signo == U_SIGINT || *signo == U_SIGTERM ||
	    *signo == U_SIGKILL;
}

int
user_main(uint64_t argc, char **argv)
{
	uint64_t signo = U_SIGTERM;
	uint64_t first_pid = 1;
	uint64_t i;
	int failed = 0;
	if (argc > 1 && u_streq(argv[1], "-s")) {
		if (argc < 4 || !parse_signal(argv[2], &signo)) {
			u_puts("usage: kill [-s INT|TERM|KILL|number] pid ...\n");
			return 1;
		}
		first_pid = 3;
	} else if (argc < 2) {
		u_puts("usage: kill [-s INT|TERM|KILL|number] pid ...\n");
		return 1;
	}
	for (i = first_pid; i < argc; i++) {
		uint64_t pid;
		long result;
		if (!parse_number(argv[i], &pid) || pid == 0) {
			u_puts("kill: invalid pid: ");
			u_puts(argv[i]);
			u_puts("\n");
			failed = 1;
			continue;
		}
		result = u_kill(pid, signo);
		if (result < 0) {
			u_puts("kill: cannot signal pid ");
			u_puts(argv[i]);
			u_puts(" (error ");
			u_put_u64((uint64_t)-result);
			u_puts(")\n");
			failed = 1;
		}
	}
	return failed;
}
