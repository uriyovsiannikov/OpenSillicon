#ifndef LIBOS_ASSERT_H
#define LIBOS_ASSERT_H
#include <stdlib.h>
#include <stdio.h>
#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((expression) ? (void)0 : \
    (fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, \
    #expression), abort()))
#endif
#endif
