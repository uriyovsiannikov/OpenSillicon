#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/cyclone_net.h"
#include "kernel/errno.h"
#include "kernel/fd.h"
#include "kernel/tcp_socket.h"

#include "core/net.h"
#include "core/socket_misc.h"
#include "core/tcp.h"

#define TCP_SOCKET_LIMIT 16U

struct tcp_socket_state {
	bool allocated;
	bool connect_pending;
	bool no_delay;
	bool reuse_address;
	bool keep_alive;
	int connect_error;
	Socket *socket;
};

static struct tcp_socket_state tcp_sockets[TCP_SOCKET_LIMIT];
static bool tcp_sockets_ready;
static uint32_t tcp_state_lock;

static void tcp_socket_close(void *context);

static uint64_t
state_guard_enter(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	while (__atomic_exchange_n(&tcp_state_lock, 1, __ATOMIC_ACQUIRE) != 0) {
		while (__atomic_load_n(&tcp_state_lock, __ATOMIC_RELAXED) != 0)
			__asm__ volatile ("pause" : : : "memory");
	}
	return flags;
#else
	while (__atomic_exchange_n(&tcp_state_lock, 1, __ATOMIC_ACQUIRE) != 0)
		__asm__ volatile ("pause" : : : "memory");
	return 0;
#endif
}

static void
state_guard_leave(uint64_t flags)
{
	__atomic_store_n(&tcp_state_lock, 0, __ATOMIC_RELEASE);
#ifdef OPEN_SILLICON_KERNEL
	__asm__ volatile ("pushq %0; popfq" : : "r" (flags) : "memory", "cc");
#else
	(void)flags;
#endif
}

static int
cyclone_error(error_t error)
{
	switch (error) {
	case NO_ERROR: return 0;
	case ERROR_WOULD_BLOCK: return -EAGAIN;
	/* Socket I/O uses DONT_WAIT; Cyclone reports an expired zero timeout
	 * when the operation would block. */
	case ERROR_TIMEOUT: return -EAGAIN;
	case ERROR_CONNECTION_FAILED:
	case ERROR_CONNECTION_REFUSED: return -ECONNREFUSED;
	case ERROR_CONNECTION_RESET: return -ECONNRESET;
	case ERROR_NOT_CONNECTED: return -ENOTCONN;
	case ERROR_ALREADY_CONNECTED: return -EISCONN;
	case ERROR_INVALID_ADDRESS: return -EADDRNOTAVAIL;
	case ERROR_INVALID_PORT: return -EINVAL;
	case ERROR_OUT_OF_MEMORY:
	case ERROR_OUT_OF_RESOURCES: return -ENOBUFS;
	default: return -EIO;
	}
}

static struct tcp_socket_state *
socket_state(struct fd_object *object)
{
	if (!tcp_sockets_ready || object == NULL ||
	    object->operations == NULL || object->operations->close != tcp_socket_close)
		return NULL;
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++)
		if (object->context == &tcp_sockets[i] && tcp_sockets[i].allocated)
			return &tcp_sockets[i];
	return NULL;
}

static int
tcp_socket_read_op(void *context, uint64_t offset, void *buffer,
	size_t length, size_t *read_length)
{
	struct tcp_socket_state *state = context;
	TcpState tcp_state;
	error_t error;
	(void)offset;
	if (state == NULL || !state->allocated || state->socket == NULL)
		return -ENOTCONN;
	tcp_state = tcpGetState(state->socket);
	if (tcp_state == TCP_STATE_LISTEN)
		return -ENOTCONN;
	error = socketReceive(state->socket, buffer, length, read_length,
	    SOCKET_FLAG_DONT_WAIT);
	if (error == ERROR_END_OF_STREAM) {
		*read_length = 0;
		return 0;
	}
	return cyclone_error(error);
}

static int
tcp_socket_write_op(void *context, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct tcp_socket_state *state = context;
	TcpState tcp_state;
	uint64_t flags;
	bool no_delay;
	error_t error;
	(void)offset;
	if (state == NULL || !state->allocated || state->socket == NULL)
		return -ENOTCONN;
	tcp_state = tcpGetState(state->socket);
	if (tcp_state == TCP_STATE_LISTEN)
		return -ENOTCONN;
	flags = state_guard_enter();
	no_delay = state->no_delay;
	state_guard_leave(flags);
	error = socketSend(state->socket, buffer, length, write_length,
	    SOCKET_FLAG_DONT_WAIT |
	    (no_delay ? SOCKET_FLAG_NO_DELAY : 0));
	return cyclone_error(error);
}

static uint32_t
tcp_socket_poll_op(void *context, uint32_t events)
{
	struct tcp_socket_state *state = context;
	TcpState tcp_state;
	uint64_t state_flags;
	uint_t flags;
	uint32_t ready = 0;
	if (state == NULL || !state->allocated || state->socket == NULL)
		return FD_POLL_ERROR;
	/* Cyclone's nonblocking operations replace the event mask with the
	 * events they wait for. Refresh the complete readiness mask before
	 * polling, including after connect has only requested CONNECTED/CLOSED. */
	socketRegisterEvents(state->socket, NULL, SOCKET_EVENT_RX_READY |
	    SOCKET_EVENT_RX_SHUTDOWN | SOCKET_EVENT_CLOSED |
	    SOCKET_EVENT_TX_READY | SOCKET_EVENT_ACCEPT);
	flags = socketGetEvents(state->socket);
	tcp_state = tcpGetState(state->socket);
	state_flags = state_guard_enter();
	if (state->connect_pending && tcp_state == TCP_STATE_CLOSED) {
		state->connect_pending = false;
		state->connect_error = ECONNREFUSED;
	}
	if (tcp_state == TCP_STATE_LISTEN) {
		if ((flags & SOCKET_EVENT_ACCEPT) != 0)
			ready |= FD_POLL_READ;
	} else {
		if ((flags & (SOCKET_EVENT_RX_READY | SOCKET_EVENT_RX_SHUTDOWN |
		    SOCKET_EVENT_CLOSED)) != 0)
			ready |= FD_POLL_READ;
		if ((tcp_state == TCP_STATE_ESTABLISHED ||
		    tcp_state == TCP_STATE_CLOSE_WAIT) &&
		    (flags & SOCKET_EVENT_TX_READY) != 0)
			ready |= FD_POLL_WRITE;
		if (state->connect_error != 0)
			ready |= FD_POLL_WRITE | FD_POLL_ERROR;
		if ((flags & SOCKET_EVENT_CLOSED) != 0 &&
		    state->socket->resetFlag)
			ready |= FD_POLL_ERROR;
	}
	state_guard_leave(state_flags);
	return ready & events;
}

static void
tcp_socket_close(void *context)
{
	uint64_t flags = state_guard_enter();
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		if (context != &tcp_sockets[i])
			continue;
		Socket *socket = tcp_sockets[i].socket;
		tcp_sockets[i] = (struct tcp_socket_state){0};
		state_guard_leave(flags);
		if (socket != NULL)
			socketClose(socket);
		return;
	}
	state_guard_leave(flags);
}

static const struct fd_operations tcp_socket_operations = {
	.read = tcp_socket_read_op,
	.write = tcp_socket_write_op,
	.poll = tcp_socket_poll_op,
	.close = tcp_socket_close
};

int
tcp_socket_system_init(void)
{
	uint64_t flags = state_guard_enter();
	if (!cyclone_net_ready() || tcp_sockets_ready) {
		state_guard_leave(flags);
		return -ENETDOWN;
	}
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++)
		tcp_sockets[i] = (struct tcp_socket_state){0};
	tcp_sockets_ready = true;
	state_guard_leave(flags);
	return 0;
}

int
tcp_socket_create_fd(struct fd_table *table, uint64_t family, uint64_t type,
	uint64_t protocol)
{
	struct tcp_socket_state *state = NULL;
	struct fd_object *object;
	Socket *socket;
	int descriptor;
	uint64_t flags;
	if (family != TCP_SOCKET_AF_INET)
		return -EAFNOSUPPORT;
	if (type != TCP_SOCKET_SOCK_STREAM ||
	    (protocol != 0 && protocol != TCP_SOCKET_PROTOCOL))
		return -EPROTONOSUPPORT;
	if (!tcp_sockets_ready || table == NULL)
		return -ENETDOWN;
	flags = state_guard_enter();
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		if (!tcp_sockets[i].allocated) {
			state = &tcp_sockets[i];
			state->allocated = true;
			break;
		}
	}
	state_guard_leave(flags);
	if (state == NULL)
		return -EMFILE;
	socket = socketOpenEx(netGetDefaultContext(), SOCKET_TYPE_STREAM,
	    SOCKET_IP_PROTO_TCP);
	if (socket == NULL) {
		flags = state_guard_enter();
		*state = (struct tcp_socket_state){0};
		state_guard_leave(flags);
		return -ENOBUFS;
	}
	state->socket = socket;
	object = fd_object_create(&tcp_socket_operations, state);
	if (object == NULL) {
		socketClose(socket);
		flags = state_guard_enter();
		*state = (struct tcp_socket_state){0};
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

bool
tcp_socket_is_object(const struct fd_object *object)
{
	uint64_t flags = state_guard_enter();
	bool result = false;
	if (!tcp_sockets_ready || object == NULL ||
	    object->operations != &tcp_socket_operations)
		goto out;
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++)
		if (object->context == &tcp_sockets[i] && tcp_sockets[i].allocated)
			result = true;
out:
	state_guard_leave(flags);
	return result;
}

int
tcp_socket_bind(struct fd_object *object,
	const struct tcp_socket_endpoint *endpoint)
{
	struct tcp_socket_state *state = socket_state(object);
	IpAddr address = { .length = sizeof(Ipv4Addr) };
	if (state == NULL)
		return -ENOTSOCK;
	if (endpoint == NULL)
		return -EINVAL;
	if ((endpoint->address[0] | endpoint->address[1] |
	    endpoint->address[2] | endpoint->address[3]) != 0 &&
	    !cyclone_net_is_local(endpoint->address))
		return -EADDRNOTAVAIL;
	address.ipv4Addr = IPV4_ADDR(endpoint->address[0], endpoint->address[1],
	    endpoint->address[2], endpoint->address[3]);
	return cyclone_error(socketBind(state->socket, &address,
	    endpoint->port != 0 ? endpoint->port : state->socket->localPort));
}

int
tcp_socket_listen(struct fd_object *object, uint64_t backlog)
{
	struct tcp_socket_state *state = socket_state(object);
	int result;
	if (state == NULL)
		return -ENOTSOCK;
	if (backlog > UINT32_MAX)
		return -EINVAL;
	result = cyclone_error(socketListen(state->socket, (uint_t)backlog));
	return result;
}

int
tcp_socket_connect(struct fd_object *object,
	const struct tcp_socket_endpoint *endpoint)
{
	struct tcp_socket_state *state = socket_state(object);
	IpAddr address = { .length = sizeof(Ipv4Addr) };
	uint64_t flags;
	bool connect_pending;
	error_t error;
	if (state == NULL)
		return -ENOTSOCK;
	if (endpoint == NULL || endpoint->port == 0)
		return -EINVAL;
	flags = state_guard_enter();
	connect_pending = state->connect_pending;
	state_guard_leave(flags);
	if (connect_pending) {
		TcpState tcp_state = tcpGetState(state->socket);
		if (tcp_state == TCP_STATE_SYN_SENT ||
		    tcp_state == TCP_STATE_SYN_RECEIVED)
			return -EINPROGRESS;
		flags = state_guard_enter();
		state->connect_pending = false;
		if (tcp_state == TCP_STATE_ESTABLISHED) {
			state_guard_leave(flags);
			return 0;
		}
		state->connect_error = ECONNREFUSED;
		state_guard_leave(flags);
		return -ECONNREFUSED;
	}
	if (!cyclone_net_can_route(endpoint->address))
		return -EHOSTUNREACH;
	flags = state_guard_enter();
	state->connect_error = 0;
	state_guard_leave(flags);
	address.ipv4Addr = IPV4_ADDR(endpoint->address[0], endpoint->address[1],
	    endpoint->address[2], endpoint->address[3]);
	(void)socketSetTimeout(state->socket, 0);
	error = socketConnect(state->socket, &address, endpoint->port);
	if (error == ERROR_TIMEOUT &&
	    (tcpGetState(state->socket) == TCP_STATE_SYN_SENT ||
	    tcpGetState(state->socket) == TCP_STATE_SYN_RECEIVED)) {
		flags = state_guard_enter();
		state->connect_pending = true;
		state_guard_leave(flags);
		return -EINPROGRESS;
	}
	return cyclone_error(error);
}

int
tcp_socket_accept(struct fd_table *table, struct fd_object *listener,
	struct tcp_socket_endpoint *peer)
{
	struct tcp_socket_state *parent = socket_state(listener);
	struct tcp_socket_state *child = NULL;
	struct fd_object *object;
	Socket *socket;
	IpAddr address = {0};
	uint16_t port = 0;
	uint64_t flags;
	int descriptor;
	if (parent == NULL || tcpGetState(parent->socket) != TCP_STATE_LISTEN)
		return -EINVAL;
	if (table == NULL)
		return -EINVAL;
	socket = socketAccept(parent->socket, &address, &port);
	if (socket == NULL)
		return -EAGAIN;
	flags = state_guard_enter();
	for (size_t i = 0; i < TCP_SOCKET_LIMIT; i++) {
		if (!tcp_sockets[i].allocated) {
			child = &tcp_sockets[i];
			*child = (struct tcp_socket_state){
				.allocated = true, .socket = socket,
				.no_delay = parent->no_delay,
				.reuse_address = parent->reuse_address,
				.keep_alive = parent->keep_alive
			};
			break;
		}
	}
	state_guard_leave(flags);
	if (child == NULL) {
		socketClose(socket);
		return -EMFILE;
	}
	object = fd_object_create(&tcp_socket_operations, child);
	if (object == NULL) {
		socketClose(socket);
		flags = state_guard_enter();
		*child = (struct tcp_socket_state){0};
		state_guard_leave(flags);
		return -ENOMEM;
	}
	descriptor = fd_install(table, object);
	if (descriptor < 0) {
		fd_object_put(object);
		return descriptor;
	}
	if (peer != NULL) {
		peer->address[0] = (uint8_t)address.ipv4Addr;
		peer->address[1] = (uint8_t)(address.ipv4Addr >> 8);
		peer->address[2] = (uint8_t)(address.ipv4Addr >> 16);
		peer->address[3] = (uint8_t)(address.ipv4Addr >> 24);
		peer->port = port;
	}
	return descriptor;
}

int
tcp_socket_shutdown(struct fd_object *object, uint64_t how)
{
	struct tcp_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (how > SOCKET_SD_BOTH ||
	    tcpGetState(state->socket) == TCP_STATE_LISTEN)
		return -EINVAL;
	return cyclone_error(socketShutdown(state->socket, (uint_t)how));
}

int
tcp_socket_get_option(struct fd_object *object, uint64_t level,
	uint64_t option, void *value, size_t *length)
{
	struct tcp_socket_state *state = socket_state(object);
	TcpState tcp_state;
	if (state == NULL)
		return -ENOTSOCK;
	if (value == NULL || length == NULL || *length < sizeof(int))
		return -EINVAL;
	if (level == TCP_SOCKET_SOL_SOCKET) {
		if (option == TCP_SOCKET_SO_ERROR) {
			uint64_t flags = state_guard_enter();
			bool pending = state->connect_pending;
			state_guard_leave(flags);
			if (pending) {
				tcp_state = tcpGetState(state->socket);
				flags = state_guard_enter();
				if (tcp_state == TCP_STATE_ESTABLISHED) {
					state->connect_pending = false;
					state->connect_error = 0;
				} else if (tcp_state == TCP_STATE_CLOSED) {
					state->connect_pending = false;
					state->connect_error = ECONNREFUSED;
				}
				state_guard_leave(flags);
			}
			flags = state_guard_enter();
			*(int *)value = state->connect_error;
			state->connect_error = 0;
			state_guard_leave(flags);
		} else if (option == TCP_SOCKET_SO_SNDBUF ||
		    option == TCP_SOCKET_SO_RCVBUF ||
		    option == TCP_SOCKET_SO_KEEPALIVE) {
			int result;
			if (option == TCP_SOCKET_SO_KEEPALIVE) {
				uint64_t flags = state_guard_enter();
				result = state->keep_alive ? 1 : 0;
				state_guard_leave(flags);
			} else {
				netLock(state->socket->netContext);
				if (option == TCP_SOCKET_SO_SNDBUF)
					result = (int)state->socket->txBufferSize;
				else
					result = (int)state->socket->rxBufferSize;
				netUnlock(state->socket->netContext);
			}
			*(int *)value = result;
		} else if (option == TCP_SOCKET_SO_REUSEADDR) {
			uint64_t flags = state_guard_enter();
			*(int *)value = state->reuse_address ? 1 : 0;
			state_guard_leave(flags);
		} else {
			return -EOPNOTSUPP;
		}
	} else if (level == TCP_SOCKET_SOL_TCP &&
	    option == TCP_SOCKET_TCP_NODELAY) {
		uint64_t flags = state_guard_enter();
		*(int *)value = state->no_delay ? 1 : 0;
		state_guard_leave(flags);
	} else {
		return -EOPNOTSUPP;
	}
	*length = sizeof(int);
	return 0;
}

int
tcp_socket_set_option(struct fd_object *object, uint64_t level,
	uint64_t option, const void *value, size_t length)
{
	struct tcp_socket_state *state = socket_state(object);
	int setting;
	error_t error;
	if (state == NULL)
		return -ENOTSOCK;
	if (value == NULL || length != sizeof(setting))
		return -EINVAL;
	setting = *(const int *)value;
	if (setting != 0 && setting != 1 &&
	    !(level == TCP_SOCKET_SOL_SOCKET &&
	    (option == TCP_SOCKET_SO_SNDBUF || option == TCP_SOCKET_SO_RCVBUF)))
		return -EINVAL;
	if (level == TCP_SOCKET_SOL_SOCKET) {
		switch (option) {
		case TCP_SOCKET_SO_REUSEADDR:
			/* Cyclone does not reject address reuse during bind. */
			{
				uint64_t flags = state_guard_enter();
				state->reuse_address = setting != 0;
				state_guard_leave(flags);
			}
			return 0;
		case TCP_SOCKET_SO_KEEPALIVE:
			error = socketEnableKeepAlive(state->socket,
			    setting ? TRUE : FALSE);
			if (error == NO_ERROR) {
				uint64_t flags = state_guard_enter();
				state->keep_alive = setting != 0;
				state_guard_leave(flags);
			}
			return cyclone_error(error);
		case TCP_SOCKET_SO_SNDBUF:
			if (setting <= 0)
				return -EINVAL;
			error = socketSetTxBufferSize(state->socket, (size_t)setting);
			return error == ERROR_INVALID_PARAMETER ||
			    error == ERROR_INVALID_SOCKET ? -EINVAL :
			    cyclone_error(error);
		case TCP_SOCKET_SO_RCVBUF:
			if (setting <= 0)
				return -EINVAL;
			error = socketSetRxBufferSize(state->socket, (size_t)setting);
			return error == ERROR_INVALID_PARAMETER ||
			    error == ERROR_INVALID_SOCKET ? -EINVAL :
			    cyclone_error(error);
		default:
			return -EOPNOTSUPP;
		}
	}
	if (level == TCP_SOCKET_SOL_TCP &&
	    option == TCP_SOCKET_TCP_NODELAY) {
		uint64_t flags = state_guard_enter();
		state->no_delay = setting != 0;
		state_guard_leave(flags);
		return 0;
	}
	return -EOPNOTSUPP;
}

int
tcp_socket_get_name(struct fd_object *object, bool peer,
	struct tcp_socket_endpoint *endpoint)
{
	struct tcp_socket_state *state = socket_state(object);
	TcpState tcp_state;
	Ipv4Addr address;
	uint16_t port;
	if (state == NULL)
		return -ENOTSOCK;
	if (endpoint == NULL)
		return -EINVAL;
	netLock(state->socket->netContext);
	tcp_state = state->socket->state;
	if (peer && tcp_state != TCP_STATE_ESTABLISHED &&
	    tcp_state != TCP_STATE_CLOSE_WAIT &&
	    tcp_state != TCP_STATE_FIN_WAIT_1 &&
	    tcp_state != TCP_STATE_FIN_WAIT_2) {
		netUnlock(state->socket->netContext);
		return -ENOTCONN;
	}
	if (peer) {
		address = state->socket->remoteIpAddr.ipv4Addr;
		port = state->socket->remotePort;
	} else {
		address = state->socket->localIpAddr.ipv4Addr;
		port = state->socket->localPort;
	}
	netUnlock(state->socket->netContext);
	endpoint->address[0] = (uint8_t)address;
	endpoint->address[1] = (uint8_t)(address >> 8);
	endpoint->address[2] = (uint8_t)(address >> 16);
	endpoint->address[3] = (uint8_t)(address >> 24);
	endpoint->port = port;
	return 0;
}
