#ifndef KERNEL_NETDEV_H
#define KERNEL_NETDEV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/netbuf.h"

#define NET_DEVICE_RX_BATCH_MAX 32U

enum net_device_state {
	NET_DEVICE_DOWN,
	NET_DEVICE_UP,
	NET_DEVICE_FAILED
};

enum net_device_error {
	NET_DEVICE_OK = 0,
	NET_DEVICE_INVALID = -1,
	NET_DEVICE_UNAVAILABLE = -2,
	NET_DEVICE_IO_ERROR = -3,
	NET_DEVICE_NO_BUFFER = -4,
	NET_DEVICE_BAD_DRIVER_CONTRACT = -5
};

struct net_device;

/*
 * initialize(): failure must leave hardware and resources stopped/released.
 * transmit(): success transfers a CORE-owned packet to DEVICE; failure must
 * leave ownership with CORE. receive(): returns at most capacity packets and
 * transfers each from DEVICE to CORE. No callback may retain ambiguous
 * ownership. Device callbacks are polled from non-interrupt context.
 */
struct net_device_ops {
	int (*initialize)(struct net_device *device);
	void (*shutdown)(struct net_device *device);
	bool (*link_up)(const struct net_device *device);
	int (*transmit)(struct net_device *device, struct netbuf_ref packet);
	int (*wait_transmit)(struct net_device *device,
	    struct netbuf_ref packet, size_t poll_limit);
	int (*receive)(struct net_device *device, size_t budget,
	    struct netbuf_ref *packets, size_t capacity, size_t *count);
};

struct net_device {
	const char *name;
	const struct net_device_ops *operations;
	void *driver_context;
	uint8_t mac_address[6];
	uint16_t mtu;
	enum net_device_state state;
};

int net_device_start(struct net_device *device);
void net_device_stop(struct net_device *device);
bool net_device_link_up(const struct net_device *device);
int net_device_transmit(struct net_device *device, struct netbuf_ref packet);
int net_device_wait_transmit(struct net_device *device,
	struct netbuf_ref packet, size_t poll_limit);
int net_device_receive(struct net_device *device, size_t budget,
	struct netbuf_ref *packets, size_t capacity, size_t *count);

#endif
