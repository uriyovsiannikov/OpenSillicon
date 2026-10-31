#include "include/os.h"

static void
start_sleep_regression(void)
{
	uint8_t child_status = 0xff;
	long child;

	if (u_sleep_ticks(0) != 0 || u_sleep_ticks(UINT64_MAX) != -22) {
		u_puts("OpenSillicon init: sleep syscall boundary test failed\n");
		u_exit(1);
	}
	child = u_fork();
	if (child < 0) {
		u_puts("OpenSillicon init: sleep test fork failed\n");
		u_exit(1);
	}
	if (child == 0) {
		if (u_sleep_ticks(1) != 0)
			u_exit(1);
		u_puts("OpenSillicon: sleep companion ran while init was blocked\n");
		u_exit(0);
	}
	if (u_sleep_ticks(5) != 0 || u_waitpid((uint64_t)child,
	    &child_status) != child || child_status != 0) {
		u_puts("OpenSillicon init: timer sleep regression failed\n");
		u_exit(1);
	}
	u_puts("OpenSillicon: sleep_ticks immediate, overflow, blocked wake passed\n");
}

int
user_main(const uint64_t *startup_stack)
{
	if (startup_stack == 0 || startup_stack[0] != 0 ||
	    startup_stack[1] != 0 || startup_stack[2] != 0) {
		u_puts("OpenSillicon init: malformed ELF startup stack\n");
		u_exit(1);
	}
	u_puts("OpenSillicon: ELF startup stack verified\n");
	start_sleep_regression();
	u_puts("OpenSillicon init: starting /bin/sh\n");
	for (;;) {
		long child = u_fork();
		uint8_t status = 0xff;

		if (child == 0) {
			(void)u_exec_path("/bin/sh");
			u_puts("OpenSillicon init: exec /bin/sh failed\n");
			u_exit(127);
		}
		if (child > 0) {
			if (u_waitpid((uint64_t)child, &status) < 0) {
				u_puts("OpenSillicon init: waitpid /bin/sh failed\n");
				status = 255;
			}
			u_puts("OpenSillicon init: shell exited status ");
			u_put_u64(status);
			u_puts("\n");
		}
		else
			u_puts("OpenSillicon init: fork /bin/sh failed\n");
		if (u_sleep_ticks(10) != 0) {
			u_puts("OpenSillicon init: restart delay failed\n");
			u_exit(1);
		}
		u_puts("OpenSillicon init: restarting /bin/sh\n");
	}
}
