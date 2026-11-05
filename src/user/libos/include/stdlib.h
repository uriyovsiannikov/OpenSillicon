#ifndef LIBOS_STDLIB_H
#define LIBOS_STDLIB_H
#include <sys/types.h>
#include <stddef.h>
#include <stddef.h>
#define RAND_MAX 2147483647
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define MB_CUR_MAX 1
extern char **environ;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void *reallocarray(void *, size_t, size_t);
void free(void *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t,
	int (*)(const void *, const void *));
char *getenv(const char *);
int setenv(const char *, const char *, int);
char *realpath(const char *, char *);
int rand(void);
void srand(unsigned int);
long random(void);
void srandom(unsigned int);
int abs(int);
long labs(long);
int atoi(const char *);
int mblen(const char *, size_t);
int mbtowc(wchar_t *, const char *, size_t);
int wctomb(char *, wchar_t);
size_t mbstowcs(wchar_t *, const char *, size_t);
size_t wcstombs(char *, const wchar_t *, size_t);
long atol(const char *);
double atof(const char *);
long strtol(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
double strtod(const char *, char **);
void exit(int) __attribute__((noreturn));
int atexit(void (*)(void));
void abort(void) __attribute__((noreturn));
int system(const char *);
#endif
