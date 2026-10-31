#include "include/os.h"

int
user_main(void)
{
	struct u_sigaction action;

	if (u_sigaction(U_SIGINT, 0, &action) != 0 ||
	    action.handler != U_SIG_DFL ||
	    u_sigaction(U_SIGTERM, 0, &action) != 0 ||
	    action.handler != U_SIG_IGN ||
	    u_kill((uint64_t)u_getpid(), U_SIGTERM) != 0)
		return 1;
	return 0;
}
