#ifndef LIBOS_SIGNAL_H
#define LIBOS_SIGNAL_H

#include <sys/types.h>

typedef unsigned long sigset_t;
typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGIO 29

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SA_RESTART 0x10000000
#define SA_NOCLDSTOP 0x00000001
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

struct sigaction {
	sighandler_t sa_handler;
	int sa_flags;
	sighandler_t sa_restorer;
	sigset_t sa_mask;
};

int sigemptyset(sigset_t *);
int sigfillset(sigset_t *);
int sigaddset(sigset_t *, int);
int sigdelset(sigset_t *, int);
int sigismember(const sigset_t *, int);
int sigaction(int, const struct sigaction *, struct sigaction *);
sighandler_t signal(int, sighandler_t);
int sigprocmask(int, const sigset_t *, sigset_t *);
int kill(pid_t, int);

#endif
