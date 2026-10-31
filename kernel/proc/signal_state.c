#include <stddef.h>

#include "kernel/signal_state.h"

void
os_signal_state_init(struct os_signal_state *state)
{
	unsigned int i;

	state->pending = 0;
	for (i = 0; i < OS_SIGNAL_COUNT; i++)
		state->actions[i] = (struct os_signal_action){0};
}

bool
os_signal_valid(unsigned int signo)
{
	return signo == OS_SIGINT || signo == OS_SIGKILL ||
	    signo == OS_SIGTERM;
}

bool
os_signal_action_valid(unsigned int signo,
	const struct os_signal_action *action)
{
	if (action == NULL || !os_signal_valid(signo))
		return false;
	if (signo == OS_SIGKILL)
		return action->handler == OS_SIGNAL_DEFAULT &&
		    action->restorer == 0;
	if (action->handler == OS_SIGNAL_DEFAULT ||
	    action->handler == OS_SIGNAL_IGNORE)
		return action->restorer == 0;
	return action->restorer != 0;
}

bool
os_signal_set_action(struct os_signal_state *state, unsigned int signo,
	const struct os_signal_action *action,
	struct os_signal_action *previous)
{
	if (state == NULL || !os_signal_action_valid(signo, action))
		return false;
	if (previous != NULL)
		*previous = state->actions[signo];
	state->actions[signo] = *action;
	return true;
}

bool
os_signal_queue(struct os_signal_state *state, unsigned int signo)
{
	if (state == NULL || !os_signal_valid(signo))
		return false;
	state->pending |= 1ULL << signo;
	return true;
}

unsigned int
os_signal_take(struct os_signal_state *state)
{
	static const unsigned int order[] = {
		OS_SIGKILL, OS_SIGTERM, OS_SIGINT
	};
	unsigned int i;

	if (state == NULL)
		return 0;
	for (i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		uint64_t bit = 1ULL << order[i];

		if ((state->pending & bit) != 0) {
			state->pending &= ~bit;
			return order[i];
		}
	}
	return 0;
}

bool
os_signal_user_stack(uint64_t interrupted_rsp, uint64_t *handler_rsp)
{
	uint64_t result;

	if (handler_rsp == NULL || interrupted_rsp < 16)
		return false;
	result = ((interrupted_rsp - 16) & ~15ULL) + 8;
	if (result >= interrupted_rsp || interrupted_rsp - result > 16)
		return false;
	*handler_rsp = result;
	return true;
}

void
os_signal_fork_state(const struct os_signal_state *parent,
	struct os_signal_state *child)
{
	unsigned int i;

	child->pending = 0;
	for (i = 0; i < OS_SIGNAL_COUNT; i++)
		child->actions[i] = parent->actions[i];
}

void
os_signal_exec_state(struct os_signal_state *state)
{
	unsigned int i;

	for (i = 0; i < OS_SIGNAL_COUNT; i++) {
		if (state->actions[i].handler != OS_SIGNAL_IGNORE)
			state->actions[i] = (struct os_signal_action){0};
	}
}
