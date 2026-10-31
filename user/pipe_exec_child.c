#include "include/os.h"

int
user_main(void)
{
	static const char message[] = "exec-inherited-pipe";

	if (u_write(4, message, sizeof(message) - 1) !=
	    (long)(sizeof(message) - 1))
		u_exit(1);
	if (u_close(4) != 0)
		u_exit(2);
	u_exit(0);
}
