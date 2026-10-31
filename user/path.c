#include "include/os.h"

size_t
u_strlen(const char *string)
{
	size_t length = 0;
	while (string[length] != '\0') length++;
	return length;
}

int
u_streq(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return *left == *right;
}

static int
append_component(char *output, size_t capacity, size_t *length,
	const char *component, size_t component_length)
{
	size_t i;
	if (component_length == 0 || (component_length == 1 && component[0] == '.'))
		return 1;
	if (component_length == 2 && component[0] == '.' && component[1] == '.') {
		if (*length > 1) {
			while (*length > 1 && output[*length - 1] != '/') (*length)--;
			if (*length > 1) (*length)--;
			output[*length] = '\0';
		}
		return 1;
	}
	if (*length + (*length > 1) + component_length >= capacity)
		return 0;
	if (*length > 1) output[(*length)++] = '/';
	for (i = 0; i < component_length; i++) output[(*length)++] = component[i];
	output[*length] = '\0';
	return 1;
}

int
u_normalize_path(const char *cwd, const char *path, char *output,
	size_t capacity)
{
	const char *source;
	size_t length = 1;
	if (path == 0 || output == 0 || capacity < 2) return 0;
	output[0] = '/';
	output[1] = '\0';
	if (path[0] != '/') {
		if (cwd == 0 || cwd[0] != '/') return 0;
		source = cwd + 1;
		while (*source != '\0') {
			const char *begin = source;
			while (*source != '\0' && *source != '/') source++;
			if (!append_component(output, capacity, &length, begin,
			    (size_t)(source - begin))) return 0;
			while (*source == '/') source++;
		}
	}
	source = path + (path[0] == '/' ? 1 : 0);
	while (*source != '\0') {
		const char *begin = source;
		while (*source != '\0' && *source != '/') source++;
		if (!append_component(output, capacity, &length, begin,
		    (size_t)(source - begin))) return 0;
		while (*source == '/') source++;
	}
	return 1;
}
