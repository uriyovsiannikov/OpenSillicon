#ifndef LIBOS_UNISTD_H
#define LIBOS_UNISTD_H
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
int close(int);
off_t lseek(int, off_t, int);
pid_t getpid(void);
void _exit(int) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
#endif
