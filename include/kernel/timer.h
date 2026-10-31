#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

bool timer_init(void);
uint64_t timer_ticks(void);

#endif
