#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdbool.h>

typedef void (*thread_entry)(void *argument);

bool scheduler_init(void);
bool thread_create(thread_entry entry, void *argument);
void thread_yield(void);

#endif
