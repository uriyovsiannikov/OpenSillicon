#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#define LIBOS_LONG_MAX 9223372036854775807L
#define LIBOS_LONG_MIN (-LIBOS_LONG_MAX - 1L)
#define LIBOS_ULONG_MAX 18446744073709551615UL

int
system(const char *command)
{
	char *arguments[4];
	pid_t child;
	int status;
	if (command == NULL)
		return 1;
	child = fork();
	if (child < 0)
		return -1;
	if (child == 0) {
		arguments[0] = (char *)"/bin/sh";
		arguments[1] = (char *)"-c";
		arguments[2] = (char *)command;
		arguments[3] = NULL;
		(void)execv(arguments[0], arguments);
		_exit(127);
	}
	if (waitpid(child, &status, 0) < 0)
		return -1;
	return status;
}

void *
bsearch(const void *key, const void *base, size_t count, size_t width,
	int (*compare)(const void *, const void *))
{
	size_t first = 0;
	if (key == NULL || base == NULL || compare == NULL || width == 0)
		return NULL;
	while (first < count) {
		size_t middle = first + (count - first) / 2;
		const unsigned char *item = (const unsigned char *)base + middle * width;
		int order = compare(key, item);
		if (order == 0)
			return (void *)item;
		if (order < 0)
			count = middle;
		else
			first = middle + 1;
	}
	return NULL;
}

static int
digit_value(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	return -1;
}

static const char *
skip_space(const char *s)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
		s++;
	return s;
}

unsigned long
strtoul(const char *text, char **end, int base)
{
	const char *original = text;
	const char *p;
	unsigned long value = 0;
	int negative = 0, any = 0, overflow = 0;
	if (text == 0) {
		errno = EINVAL;
		if (end) *end = (char *)text;
		return 0;
	}
	p = skip_space(text);
	if (*p == '+' || *p == '-') {
		negative = *p == '-';
		p++;
	}
	if (base != 0 && (base < 2 || base > 36)) {
		errno = EINVAL;
		if (end) *end = (char *)original;
		return 0;
	}
	if ((base == 0 || base == 16) && p[0] == '0' &&
	    (p[1] == 'x' || p[1] == 'X') && digit_value((unsigned char)p[2]) >= 0 &&
	    digit_value((unsigned char)p[2]) < 16) {
		base = 16;
		p += 2;
	} else if (base == 0) {
		base = *p == '0' ? 8 : 10;
	}
	for (;;) {
		int digit = digit_value((unsigned char)*p);
		if (digit < 0 || digit >= base)
			break;
		any = 1;
		if (value > (LIBOS_ULONG_MAX - (unsigned long)digit) /
		    (unsigned long)base)
			overflow = 1;
		else if (!overflow)
			value = value * (unsigned long)base + (unsigned long)digit;
		p++;
	}
	if (end)
		*end = (char *)(any ? p : original);
	if (overflow) {
		errno = ERANGE;
		return LIBOS_ULONG_MAX;
	}
	return negative ? 0UL - value : value;
}

long
strtol(const char *text, char **end, int base)
{
	const char *p = text;
	char *parsed_end;
	int negative = 0;
	unsigned long value;
	unsigned long limit;
	if (text == 0) {
		errno = EINVAL;
		if (end) *end = (char *)text;
		return 0;
	}
	p = skip_space(p);
	if (*p == '+' || *p == '-') {
		negative = *p == '-';
		p++;
	}
	limit = negative ? (unsigned long)(-(LIBOS_LONG_MIN + 1L)) + 1UL :
	    (unsigned long)LIBOS_LONG_MAX;
	value = strtoul(p, &parsed_end, base);
	if (parsed_end == p) {
		if (end) *end = (char *)text;
		return 0;
	}
	if (end) *end = parsed_end;
	if (value > limit) {
		errno = ERANGE;
		return negative ? LIBOS_LONG_MIN : LIBOS_LONG_MAX;
	}
	if (negative && value == limit)
		return LIBOS_LONG_MIN;
	return negative ? -(long)value : (long)value;
}

int atoi(const char *text) { return (int)strtol(text, 0, 10); }
int
mblen(const char *text, size_t maximum)
{
	if (text == 0)
		return 0;
	if (maximum == 0)
		return -1;
	return *text == '\0' ? 0 : 1;
}
int
mbtowc(wchar_t *wide, const char *text, size_t maximum)
{
	if (text == 0)
		return 0;
	if (maximum == 0)
		return -1;
	if (wide != 0)
		*wide = (unsigned char)*text;
	return *text == '\0' ? 0 : 1;
}
int
wctomb(char *text, wchar_t wide)
{
	if (text == 0)
		return 0;
	if (wide < 0 || wide > 0x7f)
		return -1;
	*text = (char)wide;
	return 1;
}
size_t
mbstowcs(wchar_t *wide, const char *text, size_t maximum)
{
	size_t count = 0;
	if (text == 0)
		return (size_t)-1;
	while (text[count] && count < maximum) {
		if (wide) wide[count] = (unsigned char)text[count];
		count++;
	}
	if (wide && count < maximum) wide[count] = 0;
	return count;
}
size_t
wcstombs(char *text, const wchar_t *wide, size_t maximum)
{
	size_t count = 0;
	if (wide == 0)
		return (size_t)-1;
	while (wide[count] && count < maximum) {
		if (wide[count] < 0 || wide[count] > 0x7f)
			return (size_t)-1;
		if (text) text[count] = (char)wide[count];
		count++;
	}
	if (text && count < maximum) text[count] = '\0';
	return count;
}
long atol(const char *text) { return strtol(text, 0, 10); }
int abs(int value) { return value < 0 ? -value : value; }
void *
reallocarray(void *pointer, size_t count, size_t size)
{
	if (size != 0 && count > (size_t)-1 / size) {
		errno = ENOMEM;
		return 0;
	}
	return realloc(pointer, count * size);
}
long labs(long value) { return value < 0 ? -value : value; }
void abort(void) { exit(134); }

struct lconv *
localeconv(void)
{
	static struct lconv c_locale = {
		.decimal_point = ".",
		.thousands_sep = "",
		.grouping = "",
		.int_curr_symbol = "",
		.currency_symbol = "",
		.mon_decimal_point = "",
		.mon_thousands_sep = "",
		.mon_grouping = "",
		.positive_sign = "",
		.negative_sign = "",
		.int_frac_digits = 127,
		.frac_digits = 127,
		.p_cs_precedes = 127,
		.p_sep_by_space = 127,
		.n_cs_precedes = 127,
		.n_sep_by_space = 127,
		.p_sign_posn = 127,
		.n_sign_posn = 127
	};

	return &c_locale;
}

char *
setlocale(int category, const char *locale)
{
	static char c_locale[] = "C";
	if (category < LC_CTYPE || category > LC_ALL)
		return 0;
	if (locale != 0 && locale[0] != '\0' && locale[0] != 'C' &&
	    !(locale[0] == 'P' && locale[1] == 'O' && locale[2] == 'S' &&
	    locale[3] == 'I' && locale[4] == 'X' && locale[5] == '\0'))
		return 0;
	return c_locale;
}

static void
swap_elements(unsigned char *a, unsigned char *b, size_t width)
{
	while (width--) {
		unsigned char byte = *a;
		*a++ = *b;
		*b++ = byte;
	}
}

static void
sift_down(unsigned char *base, size_t root, size_t count, size_t width,
    int (*compare)(const void *, const void *))
{
	while (root < count / 2) {
		size_t child = root * 2 + 1;
		if (child + 1 < count &&
		    compare(base + child * width, base + (child + 1) * width) < 0)
			child++;
		if (compare(base + root * width, base + child * width) >= 0)
			return;
		swap_elements(base + root * width, base + child * width, width);
		root = child;
	}
}

void
qsort(void *array, size_t count, size_t width,
    int (*compare)(const void *, const void *))
{
	unsigned char *base = array;
	size_t start, end;

	if (!base || !compare || width == 0 || count < 2)
		return;
	/* In-place heapsort avoids allocator dependencies and has bounded stack use. */
	start = count / 2;
	while (start != 0) {
		start--;
		sift_down(base, start, count, width, compare);
	}
	end = count;
	while (end > 1) {
		end--;
		swap_elements(base, base + end * width, width);
		sift_down(base, 0, end, width, compare);
	}
}
