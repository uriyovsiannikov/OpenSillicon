#ifndef KERNEL_SIGNAL_STATE_H
#define KERNEL_SIGNAL_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define OS_SIGINT 2U
#define OS_SIGHUP 1U
#define OS_SIGKILL 9U
#define OS_SIGUSR1 10U
#define OS_SIGPIPE 13U
#define OS_SIGALRM 14U
#define OS_SIGTERM 15U
#define OS_SIGCHLD 17U
#define OS_SIGIO 29U
#define OS_SIGNAL_COUNT 32U

#define OS_SIGNAL_DEFAULT 0ULL
#define OS_SIGNAL_IGNORE 1ULL

struct os_signal_action {
	uint64_t handler;
	uint64_t restorer;
};

struct os_signal_state {
	uint64_t pending;
	uint64_t blocked;
	struct os_signal_action actions[OS_SIGNAL_COUNT];
};

void os_signal_state_init(struct os_signal_state *state);
bool os_signal_valid(unsigned int signo);
bool os_signal_action_valid(unsigned int signo,
	const struct os_signal_action *action);
bool os_signal_set_action(struct os_signal_state *state, unsigned int signo,
	const struct os_signal_action *action,
	struct os_signal_action *previous);
bool os_signal_queue(struct os_signal_state *state, unsigned int signo);
unsigned int os_signal_take(struct os_signal_state *state);
bool os_signal_user_stack(uint64_t interrupted_rsp, uint64_t *handler_rsp);
void os_signal_fork_state(const struct os_signal_state *parent,
	struct os_signal_state *child);
void os_signal_exec_state(struct os_signal_state *state);

#endif
