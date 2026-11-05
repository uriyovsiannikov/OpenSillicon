#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "kernel/errno.h"
#include "kernel/cyclone_net.h"
#include "kernel/icmp_echo.h"
#include "kernel/netbuf.h"
#include "kernel/netdev.h"
#include "kernel/network_service.h"
#include "kernel/timer.h"
#include "kernel/udp_socket.h"

#define UDP_AF_INET 2U
#define UDP_SOCK_DGRAM 2U
#define UDP_PROTOCOL 17U
#define UDP_RX_BATCH 8U /* Matches the bounded Cyclone/e1000 RX batch. */

struct udp_queue_entry {
	struct udp_socket_endpoint source;
	size_t length;
	uint8_t payload[UDP_SOCKET_PAYLOAD_LIMIT];
};

struct udp_socket_state {
	bool allocated;
	bool bound;
	bool connected;
	bool send_busy;
	struct udp_socket_endpoint local;
	struct udp_socket_endpoint remote;
	struct udp_queue_entry queue[UDP_SOCKET_QUEUE_LIMIT];
	size_t queue_head;
	size_t queue_count;
	uint64_t dropped_datagrams;
};

static struct udp_socket_state sockets[UDP_SOCKET_LIMIT];
static struct net_device *network_device;
static bool system_ready;
static bool system_stopping;
static bool receive_poll_active;
static bool echo_call_active;
static uint16_t next_ephemeral = UDP_SOCKET_EPHEMERAL_FIRST;
static uint32_t socket_state_lock;

static uint64_t state_guard_enter(void);
static void state_guard_leave(uint64_t flags);

static void
cyclone_udp_receive(void *context, const uint8_t source[4],
	uint16_t source_port, const uint8_t *payload, size_t length)
{
	struct udp_socket_state *state = context;
	struct udp_queue_entry *entry;
	uint64_t flags;
	if (state == NULL || source == NULL || (length != 0 && payload == NULL) ||
	    state < sockets || state >= sockets + UDP_SOCKET_LIMIT)
		return;
	if (length > UDP_SOCKET_PAYLOAD_LIMIT)
		return;
	flags = state_guard_enter();
	if (!system_ready || !state->allocated || !state->bound ||
	    state->queue_count == UDP_SOCKET_QUEUE_LIMIT) {
		if (state->allocated && state->queue_count == UDP_SOCKET_QUEUE_LIMIT)
			state->dropped_datagrams++;
		state_guard_leave(flags);
		return;
	}
	if (state->connected && (state->remote.port != source_port ||
	    memcmp(state->remote.address, source, 4) != 0)) {
		state_guard_leave(flags);
		return;
	}
	entry = &state->queue[(state->queue_head + state->queue_count) %
	    UDP_SOCKET_QUEUE_LIMIT];
	memcpy(entry->source.address, source, 4);
	entry->source.port = source_port;
	entry->length = length;
	memcpy(entry->payload, payload, length);
	state->queue_count++;
	state_guard_leave(flags);
}

/* Protect short socket-state transitions; never hold this across a wait or
 * user-memory copy. */
static uint64_t
state_guard_enter(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	while (__atomic_exchange_n(&socket_state_lock, 1, __ATOMIC_ACQUIRE) != 0) {
		while (__atomic_load_n(&socket_state_lock, __ATOMIC_RELAXED) != 0)
			__asm__ volatile ("pause" : : : "memory");
	}
	return flags;
#else
	while (__atomic_exchange_n(&socket_state_lock, 1, __ATOMIC_ACQUIRE) != 0)
		__asm__ volatile ("pause" : : : "memory");
	return 0;
#endif
}

static void
state_guard_leave(uint64_t flags)
{
	__atomic_store_n(&socket_state_lock, 0, __ATOMIC_RELEASE);
#ifdef OPEN_SILLICON_KERNEL
	__asm__ volatile ("pushq %0; popfq" : : "r" (flags) : "memory", "cc");
#else
	(void)flags;
#endif
}

static void socket_close(void *context);
static const struct fd_operations socket_operations = {
	.close = socket_close
};

static bool
address_is_zero(const uint8_t address[4])
{
	return address[0] == 0 && address[1] == 0 &&
	    address[2] == 0 && address[3] == 0;
}

static bool
address_is_local(const uint8_t address[4])
{
	return cyclone_net_is_local(address);
}

static struct udp_socket_state *
socket_state(struct fd_object *object)
{
	if (!system_ready || object == NULL ||
	    object->operations != &socket_operations)
		return NULL;
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++)
		if (object->context == &sockets[i] && sockets[i].allocated)
			return &sockets[i];
	return NULL;
}

bool
udp_socket_is_object(const struct fd_object *object)
{
	uint64_t flags = state_guard_enter();
	bool result = false;
	if (!system_ready || object == NULL ||
	    object->operations != &socket_operations)
		goto out;
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++)
		if (object->context == &sockets[i] && sockets[i].allocated)
			result = true;
out:
	state_guard_leave(flags);
	return result;
}

static void
socket_close(void *context)
{
	uint64_t flags = state_guard_enter();
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++) {
		if (context != &sockets[i])
			continue;
		bool unregister = sockets[i].bound && cyclone_net_ready();
		uint16_t port = sockets[i].local.port;
		state_guard_leave(flags);
		if (unregister)
			(void)cyclone_net_udp_unregister(port);
		flags = state_guard_enter();
		for (size_t q = 0; q < UDP_SOCKET_QUEUE_LIMIT; q++)
			sockets[i].queue[q] = (struct udp_queue_entry){0};
		sockets[i] = (struct udp_socket_state){0};
		state_guard_leave(flags);
		return;
	}
	state_guard_leave(flags);
}

int
udp_socket_system_init(struct net_device *device)
{
	uint64_t flags = state_guard_enter();
	if (system_ready || system_stopping || device == NULL ||
	    device->state != NET_DEVICE_UP ||
	    !cyclone_net_ready()) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	state_guard_leave(flags);
	flags = state_guard_enter();
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++)
		sockets[i] = (struct udp_socket_state){0};
	echo_call_active = false;
	network_device = device;
	next_ephemeral = UDP_SOCKET_EPHEMERAL_FIRST;
	system_ready = true;
	state_guard_leave(flags);
	return 0;
}

int
udp_socket_create_fd(struct fd_table *table, uint64_t family,
	uint64_t type, uint64_t protocol)
{
	struct fd_object *object;
	size_t slot;
	int descriptor;
	uint64_t flags = state_guard_enter();
	if (!system_ready || system_stopping || table == NULL) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	state_guard_leave(flags);
	if (family != UDP_AF_INET)
		return -EAFNOSUPPORT;
	if (type != UDP_SOCK_DGRAM ||
	    (protocol != 0 && protocol != UDP_PROTOCOL))
		return -EPROTONOSUPPORT;
	flags = state_guard_enter();
	if (!system_ready || system_stopping) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	for (slot = 0; slot < UDP_SOCKET_LIMIT; slot++)
		if (!sockets[slot].allocated)
			break;
	if (slot == UDP_SOCKET_LIMIT) {
		state_guard_leave(flags);
		return -EMFILE;
	}
	sockets[slot] = (struct udp_socket_state){.allocated = true};
	state_guard_leave(flags);
	object = fd_object_create(&socket_operations, &sockets[slot]);
	if (object == NULL) {
		flags = state_guard_enter();
		sockets[slot] = (struct udp_socket_state){0};
		state_guard_leave(flags);
		return -ENOMEM;
	}
	descriptor = fd_install(table, object);
	if (descriptor < 0) {
		fd_object_put(object);
		return descriptor;
	}
	return descriptor;
}

static bool
bind_conflicts(const struct udp_socket_state *candidate,
	const struct udp_socket_endpoint *endpoint)
{
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++) {
		const struct udp_socket_state *other = &sockets[i];
		if (!other->allocated || !other->bound || other == candidate ||
		    other->local.port != endpoint->port)
			continue;
		if (address_is_zero(other->local.address) ||
		    address_is_zero(endpoint->address) ||
		    memcmp(other->local.address, endpoint->address, 4) == 0)
			return true;
	}
	return false;
}

static int
bind_socket(struct udp_socket_state *state,
	const struct udp_socket_endpoint *requested)
{
	struct udp_socket_endpoint endpoint = *requested;
	if (!address_is_zero(endpoint.address) &&
	    !address_is_local(endpoint.address))
		return -EADDRNOTAVAIL;
	uint64_t flags = state_guard_enter();
	if (state->bound)
		goto already_bound;
	if (endpoint.port == 0) {
		for (size_t attempt = 0;
		    attempt <= UDP_SOCKET_EPHEMERAL_LAST - UDP_SOCKET_EPHEMERAL_FIRST;
		    attempt++) {
			endpoint.port = next_ephemeral++;
			if (next_ephemeral < UDP_SOCKET_EPHEMERAL_FIRST)
				next_ephemeral = UDP_SOCKET_EPHEMERAL_FIRST;
			if (!bind_conflicts(state, &endpoint))
				break;
		}
		if (bind_conflicts(state, &endpoint))
			goto address_in_use;
	} else if (bind_conflicts(state, &endpoint)) {
		goto address_in_use;
	}
	state->local = endpoint;
	state->bound = true;
	state_guard_leave(flags);
	if (cyclone_net_ready() && !cyclone_net_udp_register(endpoint.port,
	    cyclone_udp_receive, state)) {
		flags = state_guard_enter();
		state->bound = false;
		state->local = (struct udp_socket_endpoint){0};
		state_guard_leave(flags);
		return -EIO;
	}
	return 0;

address_in_use:
	state_guard_leave(flags);
	return -EADDRINUSE;
already_bound:
	state_guard_leave(flags);
	return -EINVAL;
}

static int
ensure_bound(struct udp_socket_state *state)
{
	struct udp_socket_endpoint local = {0};
	uint64_t flags = state_guard_enter();
	bool bound = state->bound;
	state_guard_leave(flags);
	if (bound)
		return 0;
	int result = bind_socket(state, &local);
	if (result == -EINVAL) {
		flags = state_guard_enter();
		bound = state->bound;
		state_guard_leave(flags);
		if (bound)
			return 0;
	}
	return result;
}

int
udp_socket_bind(struct fd_object *object,
	const struct udp_socket_endpoint *endpoint)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (endpoint == NULL) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	state_guard_leave(flags);
	return bind_socket(state, endpoint);
}

int
udp_socket_connect(struct fd_object *object,
	const struct udp_socket_endpoint *endpoint)
{
	struct udp_socket_state *state;
	uint64_t flags;
	int result;
	if (endpoint == NULL || endpoint->port == 0 ||
	    !cyclone_net_can_route(endpoint->address))
		return -EHOSTUNREACH;
	flags = state_guard_enter();
	state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (state->send_busy) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	state_guard_leave(flags);
	result = ensure_bound(state);
	if (result != 0)
		return result;
	flags = state_guard_enter();
	state->remote = *endpoint;
	state->connected = true;
	state_guard_leave(flags);
	return 0;
}

int
udp_socket_send(struct fd_object *object, const uint8_t *payload,
	size_t length)
{
	struct udp_socket_endpoint destination;
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (!state->connected) {
		state_guard_leave(flags);
		return -EDESTADDRREQ;
	}
	destination = state->remote;
	state_guard_leave(flags);
	return udp_socket_sendto(object, &destination, payload, length);
}


int
udp_socket_poll(void)
{
	uint64_t flags = state_guard_enter();
	if (receive_poll_active) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	if (!system_ready || system_stopping || network_device == NULL ||
	    !cyclone_net_ready()) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	receive_poll_active = true;
	state_guard_leave(flags);
	size_t processed = cyclone_net_poll(UDP_RX_BATCH);
	bool link_up = net_device_link_up(network_device);
	flags = state_guard_enter();
	receive_poll_active = false;
	state_guard_leave(flags);
	return link_up ? (int)processed : -ENETDOWN;
}

int
network_icmp_echo(struct icmp_echo_request *request)
{
	uint64_t flags;
	int result;
	if (request == NULL || request->reserved != 0 ||
	    request->payload_length > ICMP_ECHO_PAYLOAD_LIMIT)
		return -EINVAL;
	flags = state_guard_enter();
	if (!system_ready || system_stopping || network_device == NULL ||
	    !cyclone_net_ready()) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	if (echo_call_active) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	echo_call_active = true;
	state_guard_leave(flags);
	result = cyclone_net_icmp_echo(request->destination,
	    request->identifier, request->sequence, request->payload,
	    request->payload_length, request->reply_source,
	    &request->elapsed_ticks, ICMP_ECHO_TIMEOUT_TICKS);
	flags = state_guard_enter();
	echo_call_active = false;
	state_guard_leave(flags);
	if (result == 0)
		return 0;
	return result == -2 ? -ETIMEDOUT :
	    (cyclone_net_can_route(request->destination) ? -EIO :
	    -EHOSTUNREACH);
}

int
udp_socket_sendto(struct fd_object *object,
	const struct udp_socket_endpoint *destination, const uint8_t *payload,
	size_t length)
{
	struct udp_socket_state *state;
	uint16_t source_port;
	uint64_t flags;
	int result;
	flags = state_guard_enter();
	state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	state_guard_leave(flags);
	if (destination == NULL || destination->port == 0 ||
	    (length != 0 && payload == NULL))
		return -EINVAL;
	if (length > UDP_SOCKET_PAYLOAD_LIMIT)
		return -EMSGSIZE;
	if (!cyclone_net_can_route(destination->address))
		return -EHOSTUNREACH;
	flags = state_guard_enter();
	state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	state_guard_leave(flags);
	result = ensure_bound(state);
	if (result != 0)
		return result;
	flags = state_guard_enter();
	if (state->send_busy) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	state->send_busy = true;
	source_port = state->local.port;
	state_guard_leave(flags);
	result = cyclone_net_udp_send(source_port, destination->address,
	    destination->port, payload, length);
	flags = state_guard_enter();
	state->send_busy = false;
	state_guard_leave(flags);
	return result == 0 ? (int)length : -EIO;
}

int
udp_socket_peek(struct fd_object *object, struct udp_socket_datagram *datagram)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	const struct udp_queue_entry *entry;
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (datagram == NULL) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	if (state->queue_count == 0) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	entry = &state->queue[state->queue_head];
	datagram->source = entry->source;
	datagram->length = entry->length;
	for (size_t i = 0; i < entry->length; i++)
		datagram->payload[i] = entry->payload[i];
	state_guard_leave(flags);
	return 0;
}

int
udp_socket_consume(struct fd_object *object)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (state->queue_count == 0) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	state->queue[state->queue_head] = (struct udp_queue_entry){0};
	state->queue_head = (state->queue_head + 1) % UDP_SOCKET_QUEUE_LIMIT;
	state->queue_count--;
	state_guard_leave(flags);
	return 0;
}

int
udp_socket_local_endpoint(struct fd_object *object,
	struct udp_socket_endpoint *endpoint)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	*endpoint = state->local;
	state_guard_leave(flags);
	return 0;
}

int
udp_socket_peer_endpoint(struct fd_object *object,
	struct udp_socket_endpoint *endpoint)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (!state->connected) {
		state_guard_leave(flags);
		return -ENOTCONN;
	}
	*endpoint = state->remote;
	state_guard_leave(flags);
	return 0;
}

bool
udp_socket_system_shutdown(void)
{
	uint64_t flags = state_guard_enter();
	struct net_device *device;
	if (!network_service_quiescent()) {
		state_guard_leave(flags);
		return false;
	}
	if (!system_ready) {
		state_guard_leave(flags);
		return true;
	}
	if (echo_call_active) {
		state_guard_leave(flags);
		return false;
	}
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++)
		if (sockets[i].allocated) {
			state_guard_leave(flags);
			return false;
		}
	system_stopping = true;
	device = network_device;
	state_guard_leave(flags);
	net_device_stop(device);
	if (device->state != NET_DEVICE_DOWN)
		return false;
	flags = state_guard_enter();
	network_device = NULL;
	system_ready = false;
	system_stopping = false;
	state_guard_leave(flags);
	return true;
}
