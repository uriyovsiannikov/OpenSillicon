#ifndef LIBOS_STRING_H
#define LIBOS_STRING_H
#include <sys/types.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
void *memchr(const void *, int, size_t);
size_t strlen(const char *);
size_t strnlen(const char *, size_t);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
int strcoll(const char *, const char *);
char *strcpy(char *, const char *);
char *strncpy(char *, const char *, size_t);
char *strcat(char *, const char *);
char *strncat(char *, const char *, size_t);
size_t strlcat(char *, const char *, size_t);
char *stpcpy(char *, const char *);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
char *strtok(char *, const char *);
char *strdup(const char *);
char *strerror(int);
int strncasecmp(const char *, const char *, size_t);
int strcasecmp(const char *, const char *);
void bzero(void *, size_t);
void bzero(void *, size_t);
#endif
