#include <stdlib.h>
#include <string.h>
#include <errno.h>

static char *empty_environment[] = { 0 };
char **environ = empty_environment;

/* Exec currently starts processes with an empty environment vector. */
char *
getenv(const char *name)
{
	static char default_display[] = ":0";
	size_t length;
	if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL)
		return NULL;
	length = strlen(name);
	for (size_t i = 0; environ != NULL && environ[i] != NULL; i++)
		if (strncmp(environ[i], name, length) == 0 &&
		    environ[i][length] == '=')
			return environ[i] + length + 1;
	if (strcmp(name, "DISPLAY") == 0)
		return default_display;
	return 0;
}

int
setenv(const char *name, const char *value, int overwrite)
{
	static char **owned_environment;
	size_t length, count = 0, position = 0;
	char **entries;
	char *entry;
	if (name == NULL || name[0] == '\0' || strchr(name, '=') != NULL ||
	    value == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (!overwrite && getenv(name) != NULL)
		return 0;
	length = strlen(name);
	while (environ != NULL && environ[count] != NULL) count++;
	for (position = 0; position < count; position++)
		if (strncmp(environ[position], name, length) == 0 &&
		    environ[position][length] == '=')
			break;
	entry = malloc(length + strlen(value) + 2);
	entries = malloc((count + 2) * sizeof(*entries));
	if (entry == NULL || entries == NULL) {
		free(entry);
		free(entries);
		return -1;
	}
	memcpy(entry, name, length);
	entry[length] = '=';
	strcpy(entry + length + 1, value);
	for (size_t i = 0; i < count; i++) entries[i] = environ[i];
	entries[count] = NULL;
	entries[position] = entry;
	entries[count + 1] = NULL;
	/* Strings may have been supplied by the application through environ;
	 * retain them because callers may also keep pointers from getenv. */
	free(owned_environment);
	owned_environment = entries;
	environ = entries;
	return 0;
}
