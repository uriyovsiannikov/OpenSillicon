#ifndef OPENSILLICON_CYCLONE_OS_PORT_CONFIG_H
#define OPENSILLICON_CYCLONE_OS_PORT_CONFIG_H

/* CycloneTCP's built-in no-RTOS port is only a type/configuration starting
 * point. OpenSillicon will provide its own scheduler and synchronization
 * hooks before runtime use. */
#define USE_NO_RTOS
#define GPL_LICENSE_TERMS_ACCEPTED

/* Cyclone's no-RTOS systime_t and timer intervals are expressed in ms. */
#define OS_MS_TO_SYSTICKS(n) (n)
#define OS_SYSTICKS_TO_MS(n) (n)

#include <ctype.h>
static inline int cyclone_ascii_digit(unsigned char c)
{
	return c >= '0' && c <= '9';
}
static inline int cyclone_ascii_xdigit(unsigned char c)
{
	return cyclone_ascii_digit(c) || (c >= 'a' && c <= 'f') ||
	    (c >= 'A' && c <= 'F');
}
static inline int cyclone_ascii_upper(unsigned char c)
{
	return c >= 'A' && c <= 'Z';
}
#undef isdigit
#undef isxdigit
#undef isupper
#define isdigit(c) cyclone_ascii_digit((unsigned char)(c))
#define isxdigit(c) cyclone_ascii_xdigit((unsigned char)(c))
#define isupper(c) cyclone_ascii_upper((unsigned char)(c))

#endif
