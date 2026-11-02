#include <stdlib.h>

static char *empty_environment[] = { 0 };
char **environ = empty_environment;

/* Exec currently starts processes with an empty environment vector. */
char *
getenv(const char *name)
{
	(void)name;
	return 0;
}
