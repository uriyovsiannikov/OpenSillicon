#ifndef LIBOS_STDIO_H
#define LIBOS_STDIO_H
#include <stdarg.h>
#include <sys/types.h>
typedef struct libos_FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define BUFSIZ 1024
#define P_tmpdir "/tmp"
char *mktemp(char *);
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *, const char *);
FILE *fdopen(int, const char *);
int fileno(FILE *);
int fclose(FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
void rewind(FILE *);
int fgetc(FILE *);
int fputc(int, FILE *);
int putc(int, FILE *);
int ferror(FILE *);
int feof(FILE *);
void clearerr(FILE *);
FILE *popen(const char *, const char *);
int pclose(FILE *);
int getc(FILE *);
int ungetc(int, FILE *);
char *fgets(char *, int, FILE *);
int fscanf(FILE *, const char *, ...);
int vsscanf(const char *, const char *, va_list);
int sscanf(const char *, const char *, ...);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fputs(const char *, FILE *);
int puts(const char *);
void perror(const char *);
int fflush(FILE *);
void setlinebuf(FILE *);
int printf(const char *, ...);
int vprintf(const char *, va_list);
int putchar(int);
int fprintf(FILE *, const char *, ...);
int vfprintf(FILE *, const char *, va_list);
int sprintf(char *, const char *, ...);
int vsprintf(char *, const char *, va_list);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int asprintf(char **, const char *, ...);
#endif
