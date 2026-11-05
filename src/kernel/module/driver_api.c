#include "kernel/module.h"
#include "kernel/heap.h"
#include "kernel/driver_api.h"
#include "kernel/udp_socket.h"
#include "kernel/cyclone_net.h"
#include "kernel/network_service.h"
#include "kernel/tcp_socket.h"
#include "kernel/scheduler.h"
#include "kernel/timer.h"

uint32_t
kernel_driver_api_version(void)
{
	return KERNEL_DRIVER_API_VERSION;
}

bool
kernel_driver_network_attach(struct net_device *device)
{
	static bool attached;
	if (attached || device == NULL || device->state != NET_DEVICE_UP)
		return false;
	if (!network_service_init()) {
		klog("OpenSillicon: deferred network service unavailable\n");
		return false;
	}
	if (!cyclone_net_init(device)) {
		klog("OpenSillicon: CycloneTCP initialization failed\n");
		return false;
	}
	if (udp_socket_system_init(device) != 0) {
		klog("OpenSillicon: Cyclone UDP socket adapter initialization failed\n");
		return false;
	}
	if (tcp_socket_system_init() != 0) {
		klog("OpenSillicon: Cyclone TCP socket adapter initialization failed\n");
		return false;
	}
	attached = true;
	klog("OpenSillicon: module NIC attached; requesting IPv4 configuration via DHCP\n");
	return true;
}

/* This list is the complete symbol boundary visible to loaded drivers.
 * Register once during single-threaded boot, before loading any module. */
bool
kernel_driver_api_export(void)
{
	static const struct {
		const char *name;
		const void *address;
	} exports[] = {
		{"klog", (const void *)(uintptr_t)klog},
		{"thread_yield", (const void *)(uintptr_t)thread_yield},
		{"scheduler_has_work", (const void *)(uintptr_t)scheduler_has_work},
		{"timer_wait_for_interrupt", (const void *)(uintptr_t)timer_wait_for_interrupt},
		{"timer_jiffies", (const void *)(uintptr_t)timer_jiffies},
		{"timer_monotonic_ns", (const void *)(uintptr_t)timer_monotonic_ns},
		{"kernel_driver_network_attach", (const void *)(uintptr_t)kernel_driver_network_attach},
		{"kernel_driver_api_version", (const void *)(uintptr_t)kernel_driver_api_version},
		{"klog_u64", (const void *)(uintptr_t)klog_u64},
		{"dma_alloc", (const void *)(uintptr_t)dma_alloc},
		{"dma_buffer_query", (const void *)(uintptr_t)dma_buffer_query},
		{"dma_device_acquire", (const void *)(uintptr_t)dma_device_acquire},
		{"dma_device_complete", (const void *)(uintptr_t)dma_device_complete},
		{"dma_free", (const void *)(uintptr_t)dma_free},
		{"dma_identity_addressing_available", (const void *)(uintptr_t)dma_identity_addressing_available},
		{"dma_ring_init", (const void *)(uintptr_t)dma_ring_init},
		{"dma_ring_publish", (const void *)(uintptr_t)dma_ring_publish},
		{"dma_ring_quiesce", (const void *)(uintptr_t)dma_ring_quiesce},
		{"dma_ring_reclaim", (const void *)(uintptr_t)dma_ring_reclaim},
		{"dma_ring_start", (const void *)(uintptr_t)dma_ring_start},
		{"net_device_receive", (const void *)(uintptr_t)net_device_receive},
		{"net_device_transmit", (const void *)(uintptr_t)net_device_transmit},
		{"net_device_start", (const void *)(uintptr_t)net_device_start},
		{"net_device_stop", (const void *)(uintptr_t)net_device_stop},
		{"net_device_link_up", (const void *)(uintptr_t)net_device_link_up},
		{"netbuf_pool_init", (const void *)(uintptr_t)netbuf_pool_init},
		{"netbuf_alloc", (const void *)(uintptr_t)netbuf_alloc},
		{"netbuf_capacity", (const void *)(uintptr_t)netbuf_capacity},
		{"netbuf_data", (const void *)(uintptr_t)netbuf_data},
		{"netbuf_length", (const void *)(uintptr_t)netbuf_length},
		{"netbuf_release", (const void *)(uintptr_t)netbuf_release},
		{"netbuf_set_length", (const void *)(uintptr_t)netbuf_set_length},
		{"netbuf_transfer", (const void *)(uintptr_t)netbuf_transfer},
		{"pci_config_read", (const void *)(uintptr_t)pci_config_read},
		{"pci_config_write", (const void *)(uintptr_t)pci_config_write},
		{"pci_device_claim", (const void *)(uintptr_t)pci_device_claim},
		{"pci_device_release", (const void *)(uintptr_t)pci_device_release},
		{"pci_bar_resource_valid", (const void *)(uintptr_t)pci_bar_resource_valid},
		{"pci_bar_probe_size", (const void *)(uintptr_t)pci_bar_probe_size},
		{"pci_enumerate", (const void *)(uintptr_t)pci_enumerate},
		{"pci_mmio_lease_address", (const void *)(uintptr_t)pci_mmio_lease_address},
		{"pci_mmio_map", (const void *)(uintptr_t)pci_mmio_map},
		{"pci_mmio_map_readonly", (const void *)(uintptr_t)pci_mmio_map_readonly},
		{"pci_mmio_release", (const void *)(uintptr_t)pci_mmio_release},
	};
	for (size_t i = 0; i < sizeof(exports) / sizeof(exports[0]); i++) {
		if (!kernel_module_export_symbol(exports[i].name, exports[i].address))
			return false;
	}
	return true;
}
