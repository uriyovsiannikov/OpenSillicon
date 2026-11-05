#include "../include/os.h"

_Static_assert(sizeof(struct u_process_info) == 24,
	"process snapshot ABI size mismatch");

static const char *
state_name(uint32_t state)
{
	switch (state) {
	case 0: return "ready";
	case 1: return "running";
	case 2: return "blocked";
	case 3: return "zombie";
	default: return "unknown";
	}
}

int
user_main(uint64_t argc, char **argv)
{
	struct u_process_info processes[U_PROCESS_SNAPSHOT_MAX];
	long count;
	long i;
	(void)argv;
	if (argc != 1) {
		u_puts("usage: ps\n");
		return 1;
	}
	count = u_process_snapshot(processes, U_PROCESS_SNAPSHOT_MAX);
	if (count < 0) {
		u_puts(count == -7 ? "ps: process list exceeds 128 entries\n" :
		    "ps: process snapshot failed\n");
		return 1;
	}
	u_puts("PID PPID STATE STATUS\n");
	for (i = 0; i < count; i++) {
		u_put_u64(processes[i].pid);
		u_puts(" ");
		u_put_u64(processes[i].parent_pid);
		u_puts(" ");
		u_puts(state_name(processes[i].state));
		u_puts(" ");
		u_put_u64(processes[i].exit_status);
		u_puts("\n");
	}
	return 0;
}
