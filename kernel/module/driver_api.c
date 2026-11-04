#include "kernel/module.h"
#include "kernel/heap.h"
#include "kernel/driver_api.h"
#include "kernel/udp_socket.h"
#include "kernel/network_service.h"
#include "kernel/tcp_runtime.h"

uint32_t
kernel_driver_api_version(void)
{
	return KERNEL_DRIVER_API_VERSION;
}

bool
kernel_driver_network_attach(struct net_device *device)
{
	static bool attached;
	struct netstack_config config = {
		.mac = {0x52, 0x54, 0x00, 0x15, 0x03, 0x01},
		.address = {10, 15, 3, 1},
		.netmask = {255, 255, 255, 0},
		.gateway = {10, 15, 3, 2}
	};
	if (attached || device == NULL || device->state != NET_DEVICE_UP)
		return false;
	if (udp_socket_system_init(device, &config) != 0)
		return false;
	attached = true;
	if (!network_service_init())
		klog("OpenSillicon: deferred network service unavailable\n");
	if (!tcp_runtime_init())
		klog("OpenSillicon: TCP runtime unavailable\n");
	klog("OpenSillicon: module NIC attached; IPv4 10.15.3.1/24 via 10.15.3.2\n");
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
		{"pci_enumerate", (const void *)(uintptr_t)pci_enumerate},
		{"pci_mmio_lease_address", (const void *)(uintptr_t)pci_mmio_lease_address},
		{"pci_mmio_map", (const void *)(uintptr_t)pci_mmio_map},
		{"pci_mmio_release", (const void *)(uintptr_t)pci_mmio_release},
		{"kheap_alloc", (const void *)(uintptr_t)kheap_alloc},
		{"kheap_free", (const void *)(uintptr_t)kheap_free},
	};
	for (size_t i = 0; i < sizeof(exports) / sizeof(exports[0]); i++) {
		if (!kernel_module_export_symbol(exports[i].name, exports[i].address))
			return false;
	}
	return true;
}
