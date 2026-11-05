#include "kernel/driver_api.h"
#include "kernel/e1000.h"
#include "kernel/e1000_profile.h"

struct probe_result {
	size_t count;
	struct pci_device_info device;
};

static bool
probe(const struct pci_device_info *device, void *context)
{
	struct probe_result *result = context;
	if (device->vendor_id == E1000_QEMU_VENDOR_ID &&
	    device->device_id == E1000_QEMU_DEVICE_ID &&
	    device->class_code == 2 && device->subclass == 0) {
		result->count++;
		result->device = *device;
	}
	return true;
}

int
module_init(void)
{
	struct probe_result result = {0};
	struct net_device *device;
	if (kernel_driver_api_version() != KERNEL_DRIVER_API_VERSION ||
	    !pci_enumerate(probe, &result) || result.count != 1 ||
	    !netbuf_pool_init() || !e1000_discover(&result.device))
		return -1;
	device = e1000_network_device();
	/* Once hardware initialization starts, keep this module resident even on
	 * failure: a failed hardware stop cannot permit freeing DMA callbacks. */
	if (net_device_start(device) != NET_DEVICE_OK) {
		klog("e1000: initialization failed; module retained for DMA safety\n");
		return 0;
	}
	if (!kernel_driver_network_attach(device))
		klog("e1000: network attachment failed; module retained\n");
	else
		klog("e1000: module initialized and network attached\n");
	return 0;
}

int
module_prepare_unload(void)
{
	/* Network services retain callbacks; detach support is required before
	 * these mappings can be removed, even for a failed hardware start. */
	return -1;
}
