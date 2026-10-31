#ifndef ARCH_X86_64_LAPIC_H
#define ARCH_X86_64_LAPIC_H

#include <stdbool.h>
#include <stdint.h>

bool lapic_init(void);
uint32_t lapic_id(void);

#endif
