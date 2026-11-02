#ifndef LIBOS_STDIO_H
#define LIBOS_STDIO_H
#include <sys/types.h>
typedef struct libos_FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fputs(const char *, FILE *);
int puts(const char *);
int printf(const char *, ...);
int snprintf(char *, size_t, const char *, ...);
#endif
