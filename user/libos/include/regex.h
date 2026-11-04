#ifndef LIBOS_REGEX_H
#define LIBOS_REGEX_H
#include <stddef.h>
typedef struct { int unsupported; size_t re_nsub; } regex_t;
typedef int regoff_t;
typedef struct { int rm_so, rm_eo; } regmatch_t;
#define REG_EXTENDED 1
#define REG_ICASE 2
#define REG_STARTEND 8
#define REG_NOTBOL 4
#define REG_NOMATCH 1
#define REG_BADPAT 2
int regcomp(regex_t *, const char *, int);
size_t regerror(int, const regex_t *, char *, size_t);
int regexec(const regex_t *, const char *, size_t, regmatch_t *, int);
void regfree(regex_t *);
#endif
