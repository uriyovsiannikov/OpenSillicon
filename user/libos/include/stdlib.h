#ifndef LIBOS_STDLIB_H
#define LIBOS_STDLIB_H
#include <sys/types.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
#endif
