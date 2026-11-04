#include <stdbool.h>
#include <stdint.h>

#include "kernel/errno.h"
#include "kernel/icmp_echo.h"
#include "kernel/netbuf.h"
#include "kernel/netdev.h"
#include "kernel/network_service.h"
#include "kernel/timer.h"
#include "kernel/udp_socket.h"

#define UDP_AF_INET 2U
#define UDP_SOCK_DGRAM 2U
#define UDP_PROTOCOL 17U
#define UDP_TX_POLL_LIMIT 1000000U
#define UDP_RX_BATCH 1U

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

struct icmp_echo_pending_state {
	bool active;
	bool complete;
	uint8_t destination[4];
	uint16_t identifier;
	uint16_t sequence;
	size_t payload_length;
	uint8_t payload[ICMP_ECHO_PAYLOAD_LIMIT];
	uint64_t started;
	uint64_t completed_at;
	int result;
	uint8_t reply_source[4];
};

static struct udp_socket_state sockets[UDP_SOCKET_LIMIT];
static struct net_device *network_device;
static struct netstack protocol_stack;
static bool system_ready;
static bool system_stopping;
static bool receive_poll_active;
static bool echo_call_active;
static uint16_t next_ephemeral = UDP_SOCKET_EPHEMERAL_FIRST;
static struct icmp_echo_pending_state echo_pending;

/* The current kernel is single-CPU but timer-preemptible. Protect short
 * socket/netstack state transitions from another process running in a timer
 * interrupt; never keep this guard across a wait or user-memory copy. */
static uint64_t
state_guard_enter(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
#else
	return 0;
#endif
}

static void
state_guard_leave(uint64_t flags)
{
#ifdef OPEN_SILLICON_KERNEL
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
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
	for (size_t i = 0; i < 4; i++)
		if (address[i] != protocol_stack.config.address[i])
			return false;
	return true;
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
		for (size_t q = 0; q < UDP_SOCKET_QUEUE_LIMIT; q++)
			sockets[i].queue[q] = (struct udp_queue_entry){0};
		sockets[i] = (struct udp_socket_state){0};
		state_guard_leave(flags);
		return;
	}
	state_guard_leave(flags);
}

int
udp_socket_system_init(struct net_device *device,
	const struct netstack_config *config)
{
	uint64_t flags = state_guard_enter();
	if (system_ready || system_stopping || device == NULL ||
	    device->state != NET_DEVICE_UP || config == NULL) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	state_guard_leave(flags);
	if (!netstack_init(&protocol_stack, config))
		return -EINVAL;
	flags = state_guard_enter();
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++)
		sockets[i] = (struct udp_socket_state){0};
	echo_pending = (struct icmp_echo_pending_state){0};
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
		    address_is_local(other->local.address) ==
	    address_is_local(endpoint->address))
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
	if (state->bound)
		return -EINVAL;
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
			return -EADDRINUSE;
	} else if (bind_conflicts(state, &endpoint)) {
		return -EADDRINUSE;
	}
	state->local = endpoint;
	state->bound = true;
	return 0;
}

int
udp_socket_bind(struct fd_object *object,
	const struct udp_socket_endpoint *endpoint)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	int result;
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (endpoint == NULL) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	result = bind_socket(state, endpoint);
	state_guard_leave(flags);
	return result;
}

int
udp_socket_connect(struct fd_object *object,
	const struct udp_socket_endpoint *endpoint)
{
	uint64_t flags = state_guard_enter();
	struct udp_socket_state *state = socket_state(object);
	uint8_t next_hop[4];
	int result = 0;
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (endpoint == NULL || endpoint->port == 0 ||
	    !netstack_route_ipv4(&protocol_stack, endpoint->address, next_hop)) {
		state_guard_leave(flags);
		return -EHOSTUNREACH;
	}
	if (state->send_busy) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	if (!state->bound) {
		struct udp_socket_endpoint local = {0};
		result = bind_socket(state, &local);
		if (result != 0) {
			state_guard_leave(flags);
			return result;
		}
	}
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

static int
socket_transmit(const uint8_t *frame, size_t length)
{
	struct netbuf_ref packet;
	uint8_t *data;
	int result;
	if (!netbuf_alloc(NETBUF_OWNER_CORE, &packet))
		return -ENOBUFS;
	data = netbuf_data(packet, NETBUF_OWNER_CORE);
	if (data == NULL || length > netbuf_capacity(packet, NETBUF_OWNER_CORE)) {
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return -EMSGSIZE;
	}
	for (size_t i = 0; i < length; i++)
		data[i] = frame[i];
	if (!netbuf_set_length(packet, NETBUF_OWNER_CORE, length)) {
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return -EINVAL;
	}
	result = net_device_transmit(network_device, packet);
	if (result != NET_DEVICE_OK) {
		if (netbuf_data(packet, NETBUF_OWNER_CORE) != NULL)
			(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return result == NET_DEVICE_NO_BUFFER ? -ENOBUFS : -EIO;
	}
	result = net_device_wait_transmit(network_device, packet,
	    UDP_TX_POLL_LIMIT);
	if (result == NET_DEVICE_OK) {
		if (!netbuf_release(packet, NETBUF_OWNER_CORE))
			return -EIO;
		return 0;
	}
	/* A still device-owned packet stays pinned by the driver on timeout. */
	if (netbuf_data(packet, NETBUF_OWNER_CORE) != NULL)
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
	return -ETIMEDOUT;
}

int
udp_socket_poll(void)
{
	struct netbuf_ref packets[UDP_RX_BATCH];
	uint8_t generated[NETSTACK_MAX_FRAME];
	size_t count = 0;
	int result;
	uint64_t flags = state_guard_enter();
	if (receive_poll_active) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	if (!system_ready || system_stopping || network_device == NULL ||
	    !net_device_link_up(network_device)) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	receive_poll_active = true;
	result = net_device_receive(network_device, UDP_RX_BATCH, packets,
	    UDP_RX_BATCH, &count);
	if (result != NET_DEVICE_OK) {
		receive_poll_active = false;
		state_guard_leave(flags);
		return -EIO;
	}
	if (count == 0) {
		receive_poll_active = false;
		state_guard_leave(flags);
		return 0;
	}
	{
		uint8_t *data = netbuf_data(packets[0], NETBUF_OWNER_CORE);
		size_t length = netbuf_length(packets[0], NETBUF_OWNER_CORE);
		size_t generated_length = 0;
		struct netstack_udp_view view;
		struct netstack_icmp_view icmp;
		enum netstack_event event = data == NULL ? NETSTACK_IGNORED :
		    netstack_process_frame_ex(&protocol_stack, data, length,
		    timer_ticks(), generated, sizeof(generated), &generated_length,
		    &view, &icmp);
		if (event == NETSTACK_UDP_DATAGRAM) {
			for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++) {
				struct udp_socket_state *state = &sockets[i];
				struct udp_queue_entry *entry;
				if (!state->allocated || !state->bound ||
				    state->local.port != view.destination_port ||
				    (!address_is_zero(state->local.address) &&
				    !address_is_local(state->local.address)))
					continue;
				if (state->connected) {
					bool same_remote = state->remote.port == view.source_port;
					for (size_t j = 0; same_remote && j < 4; j++)
						if (state->remote.address[j] !=
						    view.source_address[j])
							same_remote = false;
					if (!same_remote)
						continue;
				}
				if (state->queue_count == UDP_SOCKET_QUEUE_LIMIT) {
					state->dropped_datagrams++;
					break;
				}
				entry = &state->queue[(state->queue_head +
				    state->queue_count) % UDP_SOCKET_QUEUE_LIMIT];
				for (size_t j = 0; j < 4; j++)
					entry->source.address[j] = view.source_address[j];
				entry->source.port = view.source_port;
				entry->length = view.payload_length;
				for (size_t j = 0; j < view.payload_length; j++)
					entry->payload[j] = view.payload[j];
				state->queue_count++;
				break;
			}
		}
		if (echo_pending.active && !echo_pending.complete &&
		    (event == NETSTACK_ECHO_REPLY ||
		    event == NETSTACK_ICMP_ERROR)) {
			bool match = true;
			if (event == NETSTACK_ECHO_REPLY) {
				match = icmp.type == 0 && icmp.code == 0 &&
				    icmp.identifier == echo_pending.identifier &&
				    icmp.sequence == echo_pending.sequence &&
				    icmp.payload_length == echo_pending.payload_length;
				for (size_t i = 0; match && i < 4; i++)
					if (icmp.source_address[i] !=
					    echo_pending.destination[i])
						match = false;
				for (size_t i = 0; match &&
				    i < echo_pending.payload_length; i++)
					if (icmp.payload[i] != echo_pending.payload[i])
						match = false;
				if (match) {
					echo_pending.result = 0;
					for (size_t i = 0; i < 4; i++)
						echo_pending.reply_source[i] =
						    icmp.source_address[i];
				}
			} else {
				match = icmp.quoted_type == 8 &&
				    icmp.quoted_identifier == echo_pending.identifier &&
				    icmp.quoted_sequence == echo_pending.sequence;
				for (size_t i = 0; match && i < 4; i++)
					if (icmp.source_address[i] !=
					    echo_pending.destination[i] ||
					    icmp.quoted_destination[i] !=
					    echo_pending.destination[i] ||
					    icmp.quoted_source[i] !=
					    protocol_stack.config.address[i])
						match = false;
				if (match)
					echo_pending.result = -EHOSTUNREACH;
			}
			if (match)
				echo_pending.completed_at = timer_ticks();
			if (match)
				echo_pending.complete = true;
		}
		(void)netbuf_release(packets[0], NETBUF_OWNER_CORE);
		receive_poll_active = false;
		state_guard_leave(flags);
		if (generated_length != 0)
			return socket_transmit(generated, generated_length);
	}
	return 1;
}

static int
resolve_neighbor(const uint8_t address[4], uint8_t mac[6])
{
	uint8_t next_hop[4];
	uint8_t request[NETSTACK_MAX_FRAME];
	size_t request_length;
	uint64_t start;
	uint64_t flags = state_guard_enter();
	bool routed = netstack_route_ipv4(&protocol_stack, address, next_hop);
	bool found = routed && netstack_arp_lookup(&protocol_stack, next_hop,
	    timer_ticks(), mac);
	state_guard_leave(flags);
	if (!routed)
		return -EHOSTUNREACH;
	if (found)
		return 0;
	flags = state_guard_enter();
	if (!netstack_build_arp_request(&protocol_stack, next_hop, request,
	    sizeof(request), &request_length)) {
		state_guard_leave(flags);
		return -EHOSTUNREACH;
	}
	state_guard_leave(flags);
	if (socket_transmit(request, request_length) != 0)
		return -EIO;
	start = timer_ticks();
	while (timer_ticks() - start < UDP_SOCKET_POLL_TIMEOUT_TICKS) {
		int result = udp_socket_poll();
		if (result < 0)
			return result;
		flags = state_guard_enter();
		found = netstack_arp_lookup(&protocol_stack, next_hop,
		    timer_ticks(), mac);
		state_guard_leave(flags);
		if (found)
			return 0;
		__asm__ volatile ("pause" : : : "memory");
	}
		return -ETIMEDOUT;
}

static bool
icmp_destination_valid(const uint8_t address[4])
{
	uint8_t next_hop[4];
	return netstack_route_ipv4(&protocol_stack, address, next_hop);
}

int
network_icmp_echo(struct icmp_echo_request *request)
{
	uint8_t mac[6];
	uint8_t frame[NETSTACK_MAX_FRAME];
	size_t frame_length;
	uint64_t start;
	uint64_t flags;
	int result;
	if (request == NULL || request->reserved != 0 ||
	    request->payload_length > ICMP_ECHO_PAYLOAD_LIMIT)
		return -EINVAL;
	flags = state_guard_enter();
	if (!system_ready || system_stopping || network_device == NULL) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	if (echo_call_active) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	if (!icmp_destination_valid(request->destination)) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	echo_call_active = true;
	state_guard_leave(flags);
	start = timer_ticks();
	result = resolve_neighbor(request->destination, mac);
	if (result != 0)
		goto release_call;
	if (timer_ticks() - start >= ICMP_ECHO_TIMEOUT_TICKS) {
		result = -ETIMEDOUT;
		goto release_call;
	}
	flags = state_guard_enter();
	if (!system_ready || system_stopping || echo_pending.active) {
		state_guard_leave(flags);
		result = -EAGAIN;
		goto release_call;
	}
	if (!netstack_build_echo_request(&protocol_stack, mac,
	    request->destination, request->identifier, request->sequence,
	    request->payload, request->payload_length, frame, sizeof(frame),
	    &frame_length)) {
		state_guard_leave(flags);
		result = -EINVAL;
		goto release_call;
	}
	echo_pending = (struct icmp_echo_pending_state){0};
	echo_pending.active = true;
	for (size_t i = 0; i < 4; i++)
		echo_pending.destination[i] = request->destination[i];
	echo_pending.identifier = request->identifier;
	echo_pending.sequence = request->sequence;
	echo_pending.payload_length = request->payload_length;
	for (size_t i = 0; i < request->payload_length; i++)
		echo_pending.payload[i] = request->payload[i];
	echo_pending.started = start;
	state_guard_leave(flags);
	result = socket_transmit(frame, frame_length);
	if (result != 0)
		goto finish;
	if (timer_ticks() - start >= ICMP_ECHO_TIMEOUT_TICKS) {
		result = -ETIMEDOUT;
		goto finish;
	}
	for (;;) {
		flags = state_guard_enter();
		if (echo_pending.complete) {
			result = echo_pending.result;
			if (result == 0 &&
			    echo_pending.completed_at - start >=
			    ICMP_ECHO_TIMEOUT_TICKS)
				result = -ETIMEDOUT;
			if (result == 0) {
				for (size_t i = 0; i < 4; i++)
					request->reply_source[i] =
					    echo_pending.reply_source[i];
				request->elapsed_ticks = timer_ticks() - start;
				state_guard_leave(flags);
				break;
			}
			state_guard_leave(flags);
			break;
		}
		state_guard_leave(flags);
		if (timer_ticks() - start >= ICMP_ECHO_TIMEOUT_TICKS) {
			result = -ETIMEDOUT;
			break;
		}
		result = udp_socket_poll();
		if (result < 0 && result != -EAGAIN)
			break;
		__asm__ volatile ("pause" : : : "memory");
	}
finish:
	flags = state_guard_enter();
	echo_pending = (struct icmp_echo_pending_state){0};
	echo_call_active = false;
	state_guard_leave(flags);
	return result;
release_call:
	flags = state_guard_enter();
	echo_call_active = false;
	state_guard_leave(flags);
	return result;
}

int
udp_socket_sendto(struct fd_object *object,
	const struct udp_socket_endpoint *destination, const uint8_t *payload,
	size_t length)
{
	struct udp_socket_state *state;
	uint8_t destination_mac[6];
	uint8_t frame[NETSTACK_MAX_FRAME];
	size_t frame_length;
	uint16_t source_port;
	uint64_t flags = state_guard_enter();
	int result;
	state = socket_state(object);
	if (state == NULL) {
		state_guard_leave(flags);
		return -ENOTSOCK;
	}
	if (destination == NULL || destination->port == 0 ||
	    (length != 0 && payload == NULL)) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	if (length > UDP_SOCKET_PAYLOAD_LIMIT) {
		state_guard_leave(flags);
		return -EMSGSIZE;
	}
	if (!state->bound) {
		struct udp_socket_endpoint ephemeral = { .port = 0 };
		result = bind_socket(state, &ephemeral);
		if (result != 0) {
			state_guard_leave(flags);
			return result;
		}
	}
	if (state->send_busy) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	state->send_busy = true;
	source_port = state->local.port;
	state_guard_leave(flags);
	result = resolve_neighbor(destination->address, destination_mac);
	if (result == 0) {
		flags = state_guard_enter();
		if (!netstack_build_udp_frame(&protocol_stack, destination_mac,
		    destination->address, source_port, destination->port, payload,
		    length, frame, sizeof(frame), &frame_length))
			result = -EMSGSIZE;
		state_guard_leave(flags);
	}
	if (result == 0)
		result = socket_transmit(frame, frame_length);
	flags = state_guard_enter();
	state->send_busy = false;
	state_guard_leave(flags);
	return result == 0 ? (int)length : result;
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
	if (!state->bound) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	*endpoint = state->local;
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
