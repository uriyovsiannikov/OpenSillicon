#ifndef LIBOS_CTYPE_H
#define LIBOS_CTYPE_H

static inline int __libos_ctype_ascii(int c)
{
	return c >= 0 && c <= 127;
}

static inline int __libos_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int __libos_islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int __libos_isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int __libos_isxdigit(int c)
{
	return __libos_isdigit(c) || (c >= 'A' && c <= 'F') ||
	    (c >= 'a' && c <= 'f');
}
static inline int __libos_isspace(int c)
{
	return c == ' ' || (c >= '\t' && c <= '\r');
}
static inline int __libos_isalpha(int c)
{
	return __libos_isupper(c) || __libos_islower(c);
}
static inline int __libos_isalnum(int c)
{
	return __libos_isalpha(c) || __libos_isdigit(c);
}
static inline int __libos_iscntrl(int c)
{
	return (c >= 0 && c < 32) || c == 127;
}
static inline int __libos_isprint(int c) { return c >= 32 && c <= 126; }
static inline int __libos_isgraph(int c) { return c >= 33 && c <= 126; }
static inline int __libos_ispunct(int c)
{
	return __libos_isgraph(c) && !__libos_isalnum(c);
}
static inline int __libos_tolower(int c)
{
	unsigned char ch;
	if (c == -1)
		return -1;
	ch = (unsigned char)c;
	return __libos_isupper(ch) ? ch + ('a' - 'A') : ch;
}
static inline int __libos_toupper(int c)
{
	unsigned char ch;
	if (c == -1)
		return -1;
	ch = (unsigned char)c;
	return __libos_islower(ch) ? ch - ('a' - 'A') : ch;
}

#define isascii(c) __libos_ctype_ascii((unsigned char)(c))
static inline int __libos_isblank(int c) { return c == ' ' || c == '\t'; }
#define isblank(c) __libos_isblank(c)
#define isupper(c) __libos_isupper((unsigned char)(c))
#define islower(c) __libos_islower((unsigned char)(c))
#define isdigit(c) __libos_isdigit((unsigned char)(c))
#define isxdigit(c) __libos_isxdigit((unsigned char)(c))
#define isspace(c) __libos_isspace((unsigned char)(c))
#define isalpha(c) __libos_isalpha((unsigned char)(c))
#define isalnum(c) __libos_isalnum((unsigned char)(c))
#define iscntrl(c) __libos_iscntrl((unsigned char)(c))
#define isprint(c) __libos_isprint((unsigned char)(c))
#define isgraph(c) __libos_isgraph((unsigned char)(c))
#define ispunct(c) __libos_ispunct((unsigned char)(c))
#define tolower(c) __libos_tolower((c))
#define toupper(c) __libos_toupper((c))

#endif
