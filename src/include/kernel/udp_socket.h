#ifndef KERNEL_UDP_SOCKET_H
#define KERNEL_UDP_SOCKET_H

#include <stddef.h>
#include <stdint.h>

#include "kernel/fd.h"
#include "kernel/netdev.h"

#define UDP_SOCKET_LIMIT 8U
#define UDP_SOCKET_QUEUE_LIMIT 4U
#define UDP_SOCKET_PAYLOAD_LIMIT 512U
#define UDP_SOCKET_EPHEMERAL_FIRST 49152U
#define UDP_SOCKET_EPHEMERAL_LAST 65535U
#define UDP_SOCKET_POLL_TIMEOUT_TICKS 500U

/* IPv4 octets followed by a host-order port. */
struct udp_socket_endpoint {
	uint8_t address[4];
	uint16_t port;
};

_Static_assert(sizeof(struct udp_socket_endpoint) == 6,
	"native UDP endpoint ABI must be six packed bytes");

struct udp_socket_datagram {
	struct udp_socket_endpoint source;
	size_t length;
	uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
};

int udp_socket_system_init(struct net_device *device);
int udp_socket_create_fd(struct fd_table *table, uint64_t family,
	uint64_t type, uint64_t protocol);
int udp_socket_bind(struct fd_object *object,
	const struct udp_socket_endpoint *endpoint);
int udp_socket_connect(struct fd_object *object,
	const struct udp_socket_endpoint *endpoint);
int udp_socket_send(struct fd_object *object, const uint8_t *payload,
	size_t length);
int udp_socket_sendto(struct fd_object *object,
	const struct udp_socket_endpoint *destination, const uint8_t *payload,
	size_t length);
int udp_socket_peek(struct fd_object *object,
	struct udp_socket_datagram *datagram);
int udp_socket_consume(struct fd_object *object);
int udp_socket_local_endpoint(struct fd_object *object,
	struct udp_socket_endpoint *endpoint);
int udp_socket_peer_endpoint(struct fd_object *object,
	struct udp_socket_endpoint *endpoint);
int udp_socket_poll(void);
bool udp_socket_is_object(const struct fd_object *object);
bool udp_socket_system_shutdown(void);

#endif
