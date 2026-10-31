#include <assert.h>
#include <stdio.h>

#include "kernel/signal_state.h"

int
main(void)
{
	struct os_signal_state parent;
	struct os_signal_state child;
	struct os_signal_action handler = {0x400000, 0x401000};
	struct os_signal_action ignored = {OS_SIGNAL_IGNORE, 0};
	struct os_signal_action old;
	uint64_t handler_rsp;

	os_signal_state_init(&parent);
	assert(os_signal_valid(OS_SIGINT));
	assert(os_signal_valid(OS_SIGKILL));
	assert(os_signal_valid(OS_SIGTERM));
	assert(!os_signal_valid(1));
	assert(os_signal_set_action(&parent, OS_SIGINT, &handler, NULL));
	assert(!os_signal_set_action(&parent, OS_SIGKILL, &handler, NULL));
	assert(os_signal_set_action(&parent, OS_SIGTERM, &ignored, NULL));
	assert(os_signal_queue(&parent, OS_SIGINT));
	assert(os_signal_queue(&parent, OS_SIGINT));
	assert(os_signal_queue(&parent, OS_SIGTERM));
	assert(os_signal_take(&parent) == OS_SIGTERM);
	assert(os_signal_take(&parent) == OS_SIGINT);
	assert(os_signal_take(&parent) == 0);
	assert(os_signal_user_stack(0x1000, &handler_rsp));
	assert(handler_rsp % 16 == 8);
	assert(handler_rsp < 0x1000 && 0x1000 - handler_rsp <= 16);
	assert(os_signal_user_stack(0x1008, &handler_rsp));
	assert(handler_rsp % 16 == 8);
	assert(!os_signal_user_stack(15, &handler_rsp));
	assert(!os_signal_user_stack(16, NULL));

	assert(os_signal_set_action(&parent, OS_SIGINT, &handler, &old));
	assert(old.handler == handler.handler && old.restorer == handler.restorer);
	os_signal_queue(&parent, OS_SIGKILL);
	os_signal_fork_state(&parent, &child);
	assert(child.pending == 0);
	assert(child.actions[OS_SIGINT].handler == handler.handler);
	assert(child.actions[OS_SIGTERM].handler == OS_SIGNAL_IGNORE);
	assert(os_signal_queue(&child, OS_SIGINT));
	os_signal_exec_state(&child);
	assert(child.actions[OS_SIGINT].handler == OS_SIGNAL_DEFAULT);
	assert(child.actions[OS_SIGTERM].handler == OS_SIGNAL_IGNORE);
	assert((child.pending & (1ULL << OS_SIGINT)) != 0);
	puts("signal state tests: PASS");
	return 0;
}
