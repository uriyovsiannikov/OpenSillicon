#ifndef KERNEL_CYCLONE_NET_H
#define KERNEL_CYCLONE_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/netdev.h"

typedef void (*cyclone_udp_callback)(void *context, const uint8_t source[4],
	uint16_t source_port, const uint8_t *payload, size_t length);

bool cyclone_net_init(struct net_device *device);
size_t cyclone_net_poll(size_t budget);
bool cyclone_net_ready(void);
bool cyclone_net_ipv4_config(uint8_t address[4], uint8_t netmask[4],
	uint8_t gateway[4]);
bool cyclone_net_dns_server(uint8_t address[4]);
bool cyclone_net_udp_register(uint16_t port, cyclone_udp_callback callback,
	void *context);
bool cyclone_net_udp_unregister(uint16_t port);
int cyclone_net_udp_send(uint16_t source_port,
	const uint8_t destination[4], uint16_t destination_port,
	const uint8_t *payload, size_t length);
bool cyclone_net_can_route(const uint8_t destination[4]);
bool cyclone_net_is_local(const uint8_t address[4]);
int cyclone_net_icmp_echo(const uint8_t destination[4], uint16_t identifier,
	uint16_t sequence, const uint8_t *payload, size_t length,
	uint8_t reply_source[4], uint64_t *elapsed_ticks, uint64_t timeout_ticks);

#endif
