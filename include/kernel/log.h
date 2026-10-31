#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

#include <stdint.h>

void serial_init(void);
void klog(const char *message);
void klog_u64(uint64_t value);
_Noreturn void kernel_halt(void);

#endif
