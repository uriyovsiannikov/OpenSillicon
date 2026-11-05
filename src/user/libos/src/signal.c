#include <signal.h>
#include <errno.h>

#include "abi/syscall.h"
#include "internal.h"

struct kernel_signal_action {
	unsigned long handler;
	unsigned long restorer;
};

static __attribute__((naked, noreturn)) void
signal_restorer(void)
{
	__asm__ volatile ("mov $26, %eax\n\tint $0x80\n\tud2");
}

int
sigemptyset(sigset_t *set)
{
	if (set == 0) { errno = EFAULT; return -1; }
	*set = 0;
	return 0;
}

int
sigfillset(sigset_t *set)
{
	if (set == 0) { errno = EFAULT; return -1; }
	*set = ~0UL;
	return 0;
}

int
sigaddset(sigset_t *set, int signo)
{
	if (set == 0 || signo <= 0 || signo >= (int)(sizeof(*set) * 8)) {
		errno = EINVAL;
		return -1;
	}
	*set |= 1UL << signo;
	return 0;
}

int
sigdelset(sigset_t *set, int signo)
{
	if (set == 0 || signo <= 0 || signo >= (int)(sizeof(*set) * 8)) {
		errno = EINVAL;
		return -1;
	}
	*set &= ~(1UL << signo);
	return 0;
}

int
sigismember(const sigset_t *set, int signo)
{
	if (set == 0 || signo <= 0 || signo >= (int)(sizeof(*set) * 8)) {
		errno = EINVAL;
		return -1;
	}
	return ((*set >> signo) & 1UL) != 0;
}

int
sigaction(int signo, const struct sigaction *action,
	struct sigaction *old_action)
{
	struct kernel_signal_action new_kernel = {0}, old_kernel = {0};
	long result;
	if (action != 0) {
		if (action->sa_flags != 0) { errno = EOPNOTSUPP; return -1; }
		new_kernel.handler = (unsigned long)action->sa_handler;
		if (new_kernel.handler > 1)
			new_kernel.restorer = (unsigned long)signal_restorer;
	}
	result = __libos_call(SYSCALL_SIGACTION, signo,
	    action != 0 ? (long)&new_kernel : 0,
	    old_action != 0 ? (long)&old_kernel : 0, 0, 0, 0);
	if (result < 0)
		return -1;
	if (old_action != 0) {
		old_action->sa_handler = (sighandler_t)old_kernel.handler;
		old_action->sa_flags = 0;
		old_action->sa_restorer = (sighandler_t)old_kernel.restorer;
		old_action->sa_mask = 0;
	}
	return 0;
}

sighandler_t
signal(int signo, sighandler_t handler)
{
	struct sigaction action, old_action;

	action.sa_handler = handler;
	action.sa_flags = 0;
	action.sa_restorer = 0;
	action.sa_mask = 0;
	if (sigaction(signo, &action, &old_action) < 0)
		return SIG_ERR;
	return old_action.sa_handler;
}

int
sigprocmask(int how, const sigset_t *set, sigset_t *old_set)
{
	return (int)__libos_call(SYSCALL_SIGPROCMASK, how, (long)set,
	    (long)old_set, 0, 0, 0);
}

int
kill(pid_t pid, int signo)
{
	return (int)__libos_call(SYSCALL_KILL, pid, signo, 0, 0, 0, 0);
}
