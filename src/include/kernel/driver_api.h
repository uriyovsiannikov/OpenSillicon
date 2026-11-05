#ifndef KERNEL_DRIVER_API_H
#define KERNEL_DRIVER_API_H

/* Public kernel interface for in-tree and loadable device drivers. Bump the
 * major version when an exported structure or function contract changes. */
#define KERNEL_DRIVER_API_MAJOR 2U
#define KERNEL_DRIVER_API_MINOR 23U
#define KERNEL_DRIVER_API_VERSION \
	((KERNEL_DRIVER_API_MAJOR << 16) | KERNEL_DRIVER_API_MINOR)

#include "arch/x86_64/pci.h"
#include "arch/x86_64/pci_resource.h"
#include "kernel/dma.h"
#include "kernel/dma_ring.h"
#include "kernel/log.h"
#include "kernel/netbuf.h"
#include "kernel/netdev.h"

uint32_t kernel_driver_api_version(void);
/* Kernel boot only; this function is not exported to modules. */
bool kernel_driver_api_export(void);
/* Publishes a running NIC to the boot network profile. Successful attachment
 * retains its callbacks for the rest of this boot; the module must refuse unload. */
bool kernel_driver_network_attach(struct net_device *device);

#endif
