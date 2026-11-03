#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

bool timer_init(void);
uint64_t timer_ticks(void);
uint32_t timer_ap_initial_count(void);

#endif
