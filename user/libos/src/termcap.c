#include <errno.h>
#include <stddef.h>
#include <string.h>

/* Minimal termcap identity used by xterm and resize on OpenSillicon. */
int
tgetent(char *buffer, const char *name)
{
	if (name == NULL || (strcmp(name, "xterm") != 0 &&
	    strncmp(name, "xterm-", 6) != 0))
		return 0;
	if (buffer != NULL)
		memcpy(buffer, "xterm", 6);
	return 1;
}

char *
tgetstr(const char *name, char **area)
{
	static char backspace[] = "\177";
	(void)area;
	if (name != NULL && strcmp(name, "kb") == 0)
		return backspace;
	return NULL;
}
