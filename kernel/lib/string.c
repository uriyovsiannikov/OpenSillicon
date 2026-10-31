#include <stddef.h>

#include <string.h>

void *
memcpy(void *destination, const void *source, size_t length)
{
	unsigned char *output = destination;
	const unsigned char *input = source;
	size_t i;

	for (i = 0; i < length; i++)
		output[i] = input[i];
	return destination;
}

void *
memset(void *destination, int value, size_t length)
{
	unsigned char *output = destination;
	size_t i;

	for (i = 0; i < length; i++)
		output[i] = (unsigned char)value;
	return destination;
}

int
memcmp(const void *left, const void *right, size_t length)
{
	const unsigned char *a = left;
	const unsigned char *b = right;
	size_t i;

	for (i = 0; i < length; i++) {
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	}
	return 0;
}

size_t
strlen(const char *string)
{
	size_t length = 0;

	while (string[length] != '\0')
		length++;
	return length;
}

int
strcmp(const char *left, const char *right)
{
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}
	return (unsigned char)*left < (unsigned char)*right ? -1 :
	    (unsigned char)*left > (unsigned char)*right;
}

char *
strrchr(const char *string, int character)
{
	const char *last = NULL;
	char target = (char)character;

	for (;;) {
		if (*string == target)
			last = string;
		if (*string == '\0')
			return (char *)last;
		string++;
	}
}

void *
memchr(const void *memory, int character, size_t length)
{
	const unsigned char *bytes = memory;
	size_t i;

	for (i = 0; i < length; i++) {
		if (bytes[i] == (unsigned char)character)
			return (void *)(bytes + i);
	}
	return NULL;
}
