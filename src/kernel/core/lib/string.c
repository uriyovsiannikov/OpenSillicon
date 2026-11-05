#include <stddef.h>

#include <string.h>

int isalnum(int character);
int isalpha(int character);
int isdigit(int character);
int islower(int character);
int isupper(int character);
int tolower(int character);

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

void *
memmove(void *destination, const void *source, size_t length)
{
	unsigned char *out = destination;
	const unsigned char *in = source;
	if (out < in) {
		for (size_t i = 0; i < length; i++) out[i] = in[i];
	} else if (out > in) {
		while (length != 0) { length--; out[length] = in[length]; }
	}
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

int
strncmp(const char *left, const char *right, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		unsigned char a = (unsigned char)left[i];
		unsigned char b = (unsigned char)right[i];

		if (a != b)
			return a < b ? -1 : 1;
		if (a == '\0')
			return 0;
	}
	return 0;
}

int
strcasecmp(const char *left, const char *right)
{
	for (;;) {
		unsigned char a = (unsigned char)tolower((unsigned char)*left++);
		unsigned char b = (unsigned char)tolower((unsigned char)*right++);
		if (a != b) return a < b ? -1 : 1;
		if (a == '\0') return 0;
	}
}

char *
strcpy(char *destination, const char *source)
{
	char *result = destination;

	while ((*destination++ = *source++) != '\0')
		;
	return result;
}

char *
strcat(char *destination, const char *source)
{
	strcpy(destination + strlen(destination), source);
	return destination;
}

char *
strchr(const char *string, int character)
{
	char target = (char)character;

	for (;;) {
		if (*string == target)
			return (char *)string;
		if (*string == '\0')
			return NULL;
		string++;
	}
}

int
tolower(int character)
{
	if (character >= 'A' && character <= 'Z')
		return character + ('a' - 'A');
	return character;
}

int
isalnum(int character)
{
	return isalpha(character) || isdigit(character);
}

int
isalpha(int character)
{
	return islower(character) || isupper(character);
}

int
isblank(int character)
{
	return character == ' ' || character == '\t';
}

int
iscntrl(int character)
{
	return character >= 0 && (character < 0x20 || character == 0x7f);
}

int
isdigit(int character)
{
	return character >= '0' && character <= '9';
}

int
isgraph(int character)
{
	return character > ' ' && character <= '~';
}

int
islower(int character)
{
	return character >= 'a' && character <= 'z';
}

int
isprint(int character)
{
	return character >= ' ' && character <= '~';
}

int
ispunct(int character)
{
	return isgraph(character) && !isalnum(character);
}

int
isspace(int character)
{
	return character == ' ' || (character >= '\t' && character <= '\r');
}

int
isupper(int character)
{
	return character >= 'A' && character <= 'Z';
}

int
isxdigit(int character)
{
	return isdigit(character) || (character >= 'A' && character <= 'F') ||
	    (character >= 'a' && character <= 'f');
}

int
toupper(int character)
{
	if (character >= 'a' && character <= 'z')
		return character - ('a' - 'A');
	return character;
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
