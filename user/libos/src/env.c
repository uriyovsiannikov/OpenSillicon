#include <stdlib.h>

static char *empty_environment[] = { 0 };
char **environ = empty_environment;

/* Exec currently starts processes with an empty environment vector. */
char *
getenv(const char *name)
{
	static char default_display[] = ":0";
	if (name != 0 && name[0] == 'D' && name[1] == 'I' &&
	    name[2] == 'S' && name[3] == 'P' && name[4] == 'L' &&
	    name[5] == 'A' && name[6] == 'Y' && name[7] == '\0')
		return default_display;
	return 0;
}
