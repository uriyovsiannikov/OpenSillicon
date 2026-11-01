#include "include/os.h"

static int
wait_success(long child)
{
	uint8_t status = 0xff;
	return child > 0 && u_waitpid((uint64_t)child, &status) == child &&
	    status == 0;
}

int
user_main(uint64_t argc, char **argv)
{
	static const char path[] = "/init/bin/argv_probe";
	static char payload[U_EXEC_ARG_BYTES_MAX + 1];
	const char *arguments[U_EXEC_ARG_MAX + 1];
	long child;
	size_t path_length = u_strlen(path);
	size_t payload_length = U_EXEC_ARG_BYTES_MAX - path_length - 2;
	size_t i;
	(void)argc;
	(void)argv;
	arguments[0] = path;
	for (i = 0; i < payload_length; i++)
		payload[i] = 'x';
	payload[payload_length] = '\0';
	arguments[1] = payload;
	arguments[2] = 0;
	child = u_fork();
	if (child == 0) {
		(void)u_exec_path_argv(path, 2, arguments);
		u_exit(1);
	}
	if (!wait_success(child))
		return 1;
	child = u_fork();
	if (child == 0) {
		if (u_exec_path_argv(path, U_EXEC_ARG_MAX + 1, arguments) != -7)
			u_exit(1);
		for (i = payload_length; i < U_EXEC_ARG_BYTES_MAX; i++)
			payload[i] = 'x';
		payload[U_EXEC_ARG_BYTES_MAX] = '\0';
		if (u_exec_path_argv(path, 2, arguments) != -7)
			u_exit(1);
		u_exit(0);
	}
	if (!wait_success(child))
		return 1;
	u_puts("argv limits passed\n");
	return 0;
}
