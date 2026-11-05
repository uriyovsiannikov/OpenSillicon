#ifndef ARCH_X86_64_AHCI_H
#define ARCH_X86_64_AHCI_H

#include <stdbool.h>

#include "arch/x86_64/pci.h"
#include "kernel/block.h"

bool ahci_initialize(const struct pci_device_info *controller,
	struct block_device *device);

#endif
