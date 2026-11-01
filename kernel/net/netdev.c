#include "kernel/netdev.h"

static void
release_core_packets(struct netbuf_ref *packets, size_t count)
{
	for (size_t i = 0; i < count; i++)
		if (netbuf_data(packets[i], NETBUF_OWNER_CORE) != NULL)
			(void)netbuf_release(packets[i], NETBUF_OWNER_CORE);
}

int
net_device_start(struct net_device *device)
{
	if (device == NULL || device->operations == NULL ||
	    device->operations->initialize == NULL ||
	    device->operations->shutdown == NULL ||
	    device->operations->link_up == NULL ||
	    device->operations->transmit == NULL ||
	    device->operations->receive == NULL || device->mtu == 0 ||
	    device->state != NET_DEVICE_DOWN)
		return NET_DEVICE_INVALID;
	if (device->operations->initialize(device) != NET_DEVICE_OK) {
		device->state = NET_DEVICE_FAILED;
		return NET_DEVICE_IO_ERROR;
	}
	device->state = NET_DEVICE_UP;
	return NET_DEVICE_OK;
}

void
net_device_stop(struct net_device *device)
{
	if (device == NULL || device->operations == NULL ||
	    device->state != NET_DEVICE_UP)
		return;
	device->operations->shutdown(device);
	device->state = NET_DEVICE_DOWN;
}

bool
net_device_link_up(const struct net_device *device)
{
	return device != NULL && device->state == NET_DEVICE_UP &&
	    device->operations != NULL && device->operations->link_up != NULL &&
	    device->operations->link_up(device);
}

int
net_device_transmit(struct net_device *device, struct netbuf_ref packet)
{
	int result;

	if (device == NULL || device->state != NET_DEVICE_UP ||
	    device->operations == NULL || device->operations->transmit == NULL ||
	    netbuf_data(packet, NETBUF_OWNER_CORE) == NULL)
		return NET_DEVICE_UNAVAILABLE;
	result = device->operations->transmit(device, packet);
	if (result == NET_DEVICE_OK)
		return netbuf_data(packet, NETBUF_OWNER_DEVICE) != NULL ?
		    NET_DEVICE_OK : NET_DEVICE_BAD_DRIVER_CONTRACT;
	return netbuf_data(packet, NETBUF_OWNER_CORE) != NULL ? result :
	    NET_DEVICE_BAD_DRIVER_CONTRACT;
}

int
net_device_wait_transmit(struct net_device *device, struct netbuf_ref packet,
	size_t poll_limit)
{
	if (device == NULL || device->state != NET_DEVICE_UP || poll_limit == 0 ||
	    device->operations == NULL || device->operations->wait_transmit == NULL)
		return NET_DEVICE_UNAVAILABLE;
	if (netbuf_data(packet, NETBUF_OWNER_CORE) != NULL)
		return NET_DEVICE_OK;
	if (netbuf_data(packet, NETBUF_OWNER_DEVICE) == NULL)
		return NET_DEVICE_INVALID;
	if (device->operations->wait_transmit(device, packet, poll_limit) !=
	    NET_DEVICE_OK)
		return NET_DEVICE_IO_ERROR;
	return netbuf_data(packet, NETBUF_OWNER_CORE) != NULL ? NET_DEVICE_OK :
	    NET_DEVICE_BAD_DRIVER_CONTRACT;
}

int
net_device_receive(struct net_device *device, size_t budget,
	struct netbuf_ref *packets, size_t capacity, size_t *count)
{
	int result;

	if (count == NULL)
		return NET_DEVICE_INVALID;
	*count = 0;
	if (device == NULL || device->state != NET_DEVICE_UP ||
	    device->operations == NULL || device->operations->receive == NULL ||
	    packets == NULL || capacity == 0 || capacity > NET_DEVICE_RX_BATCH_MAX ||
	    budget == 0 || budget > capacity)
		return NET_DEVICE_INVALID;
	result = device->operations->receive(device, budget, packets, capacity,
	    count);
	if (result != NET_DEVICE_OK) {
		if (*count != 0) {
			release_core_packets(packets, *count < capacity ? *count : capacity);
			*count = 0;
			return NET_DEVICE_BAD_DRIVER_CONTRACT;
		}
		return result;
	}
	if (*count > budget || *count > capacity) {
		release_core_packets(packets, *count < capacity ? *count : capacity);
		*count = 0;
		return NET_DEVICE_BAD_DRIVER_CONTRACT;
	}
	for (size_t i = 0; i < *count; i++) {
		if (netbuf_data(packets[i], NETBUF_OWNER_CORE) == NULL) {
			for (size_t j = 0; j < i; j++)
				(void)netbuf_release(packets[j], NETBUF_OWNER_CORE);
			*count = 0;
			return NET_DEVICE_BAD_DRIVER_CONTRACT;
		}
	}
	return NET_DEVICE_OK;
}
