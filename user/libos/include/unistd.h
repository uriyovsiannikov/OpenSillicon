#ifndef LIBOS_UNISTD_H
#define LIBOS_UNISTD_H
#include <stdlib.h>
#include <sys/types.h>
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
int access(const char *, int);
int chdir(const char *);
int mkdir(const char *, unsigned int);
int rmdir(const char *);
int dup(int);
pid_t wait(int *);
int setpgrp(void);
char *ttyname(int);
unsigned int alarm(unsigned int);
int chown(const char *, uid_t, gid_t);
int chmod(const char *, unsigned int);
void endgrent(void);
char *getcwd(char *, size_t);
int getpagesize(void);
int usleep(unsigned int);
int isatty(int);
int execvp(const char *, char *const []);
int execlp(const char *, const char *, ...);
int mkstemp(char *);
int putenv(char *);
int unlink(const char *);
int gethostname(char *, size_t);
#define _SC_OPEN_MAX 4
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_IOV_MAX 60
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
unsigned int sleep(unsigned int);
int close(int);
int pipe(int descriptors[2]);
int dup2(int source, int destination);
off_t lseek(int, off_t, int);
pid_t getpid(void);
pid_t getppid(void);
long sysconf(int name);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int getresuid(uid_t *, uid_t *, uid_t *);
int getresgid(gid_t *, gid_t *, gid_t *);
int setuid(uid_t);
int seteuid(uid_t);
int setgid(gid_t);
int setegid(gid_t);
pid_t fork(void);
pid_t waitpid(pid_t, int *, int);
int execv(const char *, char *const []);
int execl(const char *, const char *, ...);
void _exit(int) __attribute__((noreturn));
#endif
