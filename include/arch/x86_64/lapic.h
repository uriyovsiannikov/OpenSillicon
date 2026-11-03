#ifndef ARCH_X86_64_LAPIC_H
#define ARCH_X86_64_LAPIC_H

#include <stdbool.h>
#include <stdint.h>

bool lapic_init(void);
bool lapic_ap_init(uint32_t expected_apic_id);
uint32_t lapic_id(void);
bool lapic_send_ipi(uint32_t apic_id, uint8_t vector);
bool lapic_timer_calibrate(uint32_t frequency, uint32_t *initial_count);
bool lapic_timer_start(uint8_t vector, uint32_t initial_count);
void lapic_eoi(void);

#endif
