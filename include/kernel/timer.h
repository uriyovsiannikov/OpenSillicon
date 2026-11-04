#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdbool.h>
#include <stdint.h>

bool timer_init(void);
uint64_t timer_ticks(void);
uint32_t timer_ap_initial_count(void);
/* Wait only outside locks/guards, after timer_init. Temporarily enables
 * interrupts and restores the caller's interrupt-enable state on return. */
void timer_wait_for_interrupt(void);

#endif
