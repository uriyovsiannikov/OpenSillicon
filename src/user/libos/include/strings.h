#ifndef LIBOS_STRINGS_H
#define LIBOS_STRINGS_H

#include <sys/types.h>

int strcasecmp(const char *, const char *);
int strncasecmp(const char *, const char *, size_t);

#endif
