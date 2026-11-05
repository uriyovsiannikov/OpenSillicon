#ifndef ARCH_X86_64_IOAPIC_H
#define ARCH_X86_64_IOAPIC_H

#include <stdbool.h>

#include "arch/x86_64/acpi.h"

bool ioapic_init(const struct acpi_madt_info *madt);

#endif
