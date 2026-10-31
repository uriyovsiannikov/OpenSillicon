#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/netbuf.h"
#include "kernel/netdev.h"

struct fake_driver {
	bool initialized;
	bool linked;
	unsigned int stopped;
	bool fail_tx;
};

static int
fake_initialize(struct net_device *device)
{
	struct fake_driver *driver = device->driver_context;
	driver->initialized = true;
	return NET_DEVICE_OK;
}

static void
fake_shutdown(struct net_device *device)
{
	struct fake_driver *driver = device->driver_context;
	driver->stopped++;
	driver->initialized = false;
}

static bool
fake_link_up(const struct net_device *device)
{
	const struct fake_driver *driver = device->driver_context;
	return driver->linked;
}

static int
fake_transmit(struct net_device *device, struct netbuf_ref packet)
{
	struct fake_driver *driver = device->driver_context;
	if (driver->fail_tx)
		return NET_DEVICE_NO_BUFFER;
	assert(netbuf_transfer(packet, NETBUF_OWNER_CORE, NETBUF_OWNER_DEVICE));
	return NET_DEVICE_OK;
}

static int
fake_receive(struct net_device *device, size_t budget,
	struct netbuf_ref *packets, size_t capacity, size_t *count)
{
	struct netbuf_ref packet;
	uint8_t *data;
	(void)device;
	assert(budget != 0 && capacity != 0);
	if (!netbuf_alloc(NETBUF_OWNER_DEVICE, &packet))
		return NET_DEVICE_NO_BUFFER;
	data = netbuf_data(packet, NETBUF_OWNER_DEVICE);
	data[0] = 0xa5;
	assert(netbuf_set_length(packet, NETBUF_OWNER_DEVICE, 1));
	assert(netbuf_transfer(packet, NETBUF_OWNER_DEVICE, NETBUF_OWNER_CORE));
	packets[0] = packet;
	*count = 1;
	return NET_DEVICE_OK;
}

static const struct net_device_ops fake_ops = {
	.initialize = fake_initialize,
	.shutdown = fake_shutdown,
	.link_up = fake_link_up,
	.transmit = fake_transmit,
	.receive = fake_receive
};

static void
test_packet_pool(void)
{
	struct netbuf_ref refs[NETBUF_POOL_CAPACITY];
	struct netbuf_ref stale;

	assert(netbuf_pool_init());
	assert(netbuf_pool_available() == NETBUF_POOL_CAPACITY);
	assert(!netbuf_alloc((enum netbuf_owner)99, &refs[0]));
	for (size_t i = 0; i < NETBUF_POOL_CAPACITY; i++) {
		assert(netbuf_alloc(NETBUF_OWNER_CORE, &refs[i]));
		assert(netbuf_data(refs[i], NETBUF_OWNER_CORE) != NULL);
		assert(netbuf_capacity(refs[i], NETBUF_OWNER_CORE) ==
		    NETBUF_CAPACITY - NETBUF_HEADROOM);
	}
	stale = refs[1];
	assert(!netbuf_alloc(NETBUF_OWNER_CORE, &refs[0]));
	assert(netbuf_set_length(refs[1], NETBUF_OWNER_CORE,
	    NETBUF_CAPACITY - NETBUF_HEADROOM));
	assert(!netbuf_set_length(refs[1], NETBUF_OWNER_CORE, NETBUF_CAPACITY));
	assert(netbuf_transfer(refs[1], NETBUF_OWNER_CORE, NETBUF_OWNER_DEVICE));
	assert(!netbuf_transfer(refs[1], NETBUF_OWNER_DEVICE,
	    (enum netbuf_owner)99));
	assert(netbuf_data(refs[1], NETBUF_OWNER_CORE) == NULL);
	assert(!netbuf_release(refs[1], NETBUF_OWNER_CORE));
	assert(netbuf_release(refs[1], NETBUF_OWNER_DEVICE));
	assert(!netbuf_release(stale, NETBUF_OWNER_CORE));
	assert(netbuf_alloc(NETBUF_OWNER_CORE, &refs[1]));
	assert(refs[1].slot == stale.slot && refs[1].generation != stale.generation);
	for (size_t i = 0; i < NETBUF_POOL_CAPACITY; i++)
		assert(netbuf_release(refs[i], NETBUF_OWNER_CORE));
	assert(netbuf_pool_available() == NETBUF_POOL_CAPACITY);
}

static void
test_device_contract(void)
{
	struct fake_driver driver = {.linked = true};
	struct net_device device = {
		.name = "fake0",
		.operations = &fake_ops,
		.driver_context = &driver,
		.mtu = 1500,
		.state = NET_DEVICE_DOWN
	};
	struct netbuf_ref packet;
	struct netbuf_ref received[2];
	size_t count;

	assert(net_device_start(&device) == NET_DEVICE_OK);
	assert(device.state == NET_DEVICE_UP && driver.initialized);
	assert(net_device_link_up(&device));
	assert(netbuf_alloc(NETBUF_OWNER_CORE, &packet));
	assert(net_device_transmit(&device, packet) == NET_DEVICE_OK);
	assert(netbuf_release(packet, NETBUF_OWNER_DEVICE));
	driver.fail_tx = true;
	assert(netbuf_alloc(NETBUF_OWNER_CORE, &packet));
	assert(net_device_transmit(&device, packet) == NET_DEVICE_NO_BUFFER);
	assert(netbuf_release(packet, NETBUF_OWNER_CORE));
	assert(net_device_receive(&device, 1, received, 2, &count) == NET_DEVICE_OK);
	assert(count == 1 && netbuf_length(received[0], NETBUF_OWNER_CORE) == 1);
	assert(netbuf_data(received[0], NETBUF_OWNER_CORE)[0] == 0xa5);
	assert(netbuf_release(received[0], NETBUF_OWNER_CORE));
	assert(net_device_receive(&device, NET_DEVICE_RX_BATCH_MAX + 1,
	    received, 2, &count) == NET_DEVICE_INVALID);
	net_device_stop(&device);
	assert(device.state == NET_DEVICE_DOWN && driver.stopped == 1);
}

int
main(void)
{
	test_packet_pool();
	test_device_contract();
	puts("Bounded network packet pool and device ownership tests passed");
	return 0;
}
