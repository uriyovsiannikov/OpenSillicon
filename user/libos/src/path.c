#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

char *
realpath(const char *path, char *resolved)
{
	char output[PATH_MAX];
	size_t length;
	struct stat status;
	if (path == NULL || path[0] == '\0') {
		errno = path == NULL ? EINVAL : ENOENT;
		return NULL;
	}
	if (path[0] == '/') {
		output[0] = '/';
		output[1] = '\0';
	} else if (getcwd(output, sizeof(output)) == NULL) {
		return NULL;
	}
	length = strlen(output);
	while (*path != '\0') {
		const char *component;
		size_t size;
		while (*path == '/') path++;
		if (*path == '\0') break;
		component = path;
		while (*path != '\0' && *path != '/') path++;
		size = (size_t)(path - component);
		if (size == 1 && component[0] == '.') continue;
		if (size == 2 && component[0] == '.' && component[1] == '.') {
			while (length > 1 && output[length - 1] != '/') length--;
			if (length > 1) length--;
			output[length] = '\0';
			continue;
		}
		if (length + (length > 1) + size >= sizeof(output)) {
			errno = ENAMETOOLONG;
			return NULL;
		}
		if (length > 1) output[length++] = '/';
		memcpy(output + length, component, size);
		length += size;
		output[length] = '\0';
		if (stat(output, &status) != 0) return NULL;
		if (S_ISLNK(status.st_mode)) {
			errno = EOPNOTSUPP;
			return NULL;
		}
		if (*path == '/' && !S_ISDIR(status.st_mode)) {
			errno = ENOTDIR;
			return NULL;
		}
	}
	if (stat(output, &status) != 0) return NULL;
	if (resolved == NULL) {
		resolved = malloc(length + 1);
		if (resolved == NULL) return NULL;
	}
	memcpy(resolved, output, length + 1);
	return resolved;
}
