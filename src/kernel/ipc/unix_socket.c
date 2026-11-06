#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/process.h"
#include "kernel/spinlock.h"
#include "kernel/unix_socket.h"
#include "abi/unix_socket.h"

#define UNIX_SOCKET_LIMIT 32U
#define UNIX_SOCKET_BACKLOG 8U

struct unix_socket_state {
	bool used;
	bool closed;
	bool listener;
	bool bound;
	bool connected;
	bool read_shutdown;
	bool write_shutdown;
	struct unix_socket_state *peer;
	uint64_t owner_pid;
	uint64_t peer_pid;
	uint32_t owner_uid;
	uint32_t owner_gid;
	uint32_t peer_uid;
	uint32_t peer_gid;
	size_t send_capacity;
	size_t receive_capacity;
	uint8_t path[UNIX_SOCKET_PATH_MAX];
	size_t path_length;
	uint8_t bytes[UNIX_SOCKET_BUFFER_SIZE];
	size_t head;
	size_t count;
	struct unix_socket_state *pending[UNIX_SOCKET_BACKLOG];
	size_t pending_head;
	size_t pending_count;
	uint8_t read_wait;
	uint8_t write_wait;
};

static struct unix_socket_state sockets[UNIX_SOCKET_LIMIT];
static const struct fd_operations socket_operations;
static struct spinlock socket_lock = SPINLOCK_INITIALIZER;

static uint64_t
state_guard_enter(void)
{
	return spinlock_lock_irqsave(&socket_lock);
}

static void
state_guard_leave(uint64_t flags)
{
	spinlock_unlock_irqrestore(&socket_lock, flags);
}

static struct unix_socket_state *
socket_allocate(void)
{
	for (size_t i = 0; i < UNIX_SOCKET_LIMIT; i++) {
		if (!sockets[i].used) {
			sockets[i] = (struct unix_socket_state){
				.used = true,
				.send_capacity = UNIX_SOCKET_BUFFER_SIZE,
				.receive_capacity = UNIX_SOCKET_BUFFER_SIZE
			};
			return &sockets[i];
		}
	}
	return NULL;
}

static void
socket_release_if_closed(struct unix_socket_state *state)
{
	if (state == NULL || !state->closed)
		return;
	if (state->peer != NULL && !state->peer->closed)
		return;
	if (state->peer != NULL) {
		struct unix_socket_state *peer = state->peer;
		peer->peer = NULL;
		state->peer = NULL;
		if (peer->closed)
			*peer = (struct unix_socket_state){0};
	}
	*state = (struct unix_socket_state){0};
}

static struct unix_socket_state *
socket_state(const struct fd_object *object)
{
	if (object == NULL || object->operations != &socket_operations)
		return NULL;
	struct unix_socket_state *state = object->context;
	for (size_t i = 0; i < UNIX_SOCKET_LIMIT; i++)
		if (state == &sockets[i] && state->used)
			return state;
	return NULL;
}

static int
socket_read(void *context, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct unix_socket_state *state = context;
	uint64_t flags;
	(void)offset;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*read_length = 0;
	if (length == 0)
		return 0;
	flags = state_guard_enter();
	if (state->listener) {
		state_guard_leave(flags);
		return -EOPNOTSUPP;
	}
	if (state->read_shutdown) {
		state_guard_leave(flags);
		return 0;
	}
	if (state->peer == NULL) {
		state_guard_leave(flags);
		return -ENOTCONN;
	}
	if (state->count == 0) {
		int result = state->peer->closed || state->peer->write_shutdown ?
		    0 : -EAGAIN;
		state_guard_leave(flags);
		return result;
	}
	size_t amount = state->count < length ? state->count : length;
	for (size_t i = 0; i < amount; i++)
		((uint8_t *)buffer)[i] = state->bytes[(state->head + i) %
		    UNIX_SOCKET_BUFFER_SIZE];
	state->head = (state->head + amount) % UNIX_SOCKET_BUFFER_SIZE;
	state->count -= amount;
	*read_length = amount;
	if (state->peer != NULL)
		process_wake_channel(&state->peer->write_wait);
	fd_readiness_wake();
	state_guard_leave(flags);
	return 0;
}

static int
socket_write(void *context, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct unix_socket_state *state = context;
	uint64_t flags;
	(void)offset;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*write_length = 0;
	if (length == 0)
		return 0;
	flags = state_guard_enter();
	if (state->listener) {
		state_guard_leave(flags);
		return -EOPNOTSUPP;
	}
	if (state->peer == NULL) {
		state_guard_leave(flags);
		return state->connected ? -EPIPE : -ENOTCONN;
	}
	if (state->write_shutdown || state->peer->closed ||
	    state->peer->read_shutdown) {
		state_guard_leave(flags);
		return -EPIPE;
	}
	struct unix_socket_state *peer = state->peer;
	size_t capacity = state->send_capacity < peer->receive_capacity ?
	    state->send_capacity : peer->receive_capacity;
	if (peer->count >= capacity) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	size_t amount = capacity - peer->count;
	if (amount > length)
		amount = length;
	size_t tail = (peer->head + peer->count) % UNIX_SOCKET_BUFFER_SIZE;
	for (size_t i = 0; i < amount; i++)
		peer->bytes[(tail + i) % UNIX_SOCKET_BUFFER_SIZE] =
		    ((const uint8_t *)buffer)[i];
	peer->count += amount;
	*write_length = amount;
	process_wake_channel(&peer->read_wait);
	fd_readiness_wake();
	state_guard_leave(flags);
	return 0;
}

static uint32_t
socket_poll(void *context, uint32_t events)
{
	struct unix_socket_state *state = context;
	uint32_t ready = 0;
	uint64_t flags = state_guard_enter();
	if (state->listener) {
		if ((events & FD_POLL_READ) != 0 && state->pending_count != 0)
			ready |= FD_POLL_READ;
	} else if (state->connected &&
	    (state->peer == NULL || state->peer->closed)) {
		if ((events & FD_POLL_READ) != 0 &&
		    (state->count != 0 || state->peer == NULL || state->peer->closed))
			ready |= FD_POLL_READ;
		if ((events & FD_POLL_ERROR) != 0)
			ready |= FD_POLL_ERROR;
	} else if (!state->connected) {
		/* Unconnected streams are neither readable nor writable. */
	} else {
		if ((events & FD_POLL_READ) != 0 &&
		    (state->read_shutdown || state->count != 0 ||
		    state->peer->write_shutdown))
			ready |= FD_POLL_READ;
		if ((events & FD_POLL_WRITE) != 0) {
			size_t capacity = state->send_capacity <
			    state->peer->receive_capacity ? state->send_capacity :
			    state->peer->receive_capacity;
			if (state->write_shutdown || state->peer->read_shutdown ||
			    state->peer->count < capacity)
				ready |= FD_POLL_WRITE;
		}
		if ((events & FD_POLL_ERROR) != 0 &&
		    (state->write_shutdown || state->peer->read_shutdown))
			ready |= FD_POLL_ERROR;
	}
	state_guard_leave(flags);
	return ready;
}

static void
socket_close(void *context)
{
	struct unix_socket_state *state = context;
	uint64_t flags = state_guard_enter();
	state->closed = true;
	state->bound = false;
	if (state->peer != NULL) {
		process_wake_channel(&state->peer->read_wait);
		process_wake_channel(&state->peer->write_wait);
	}
	fd_readiness_wake();
	for (size_t i = 0; i < state->pending_count; i++) {
		struct unix_socket_state *pending = state->pending[
		    (state->pending_head + i) % UNIX_SOCKET_BACKLOG];
		if (pending != NULL) {
			pending->closed = true;
			socket_release_if_closed(pending);
		}
	}
	socket_release_if_closed(state);
	state_guard_leave(flags);
}

static const struct fd_operations socket_operations = {
	.read = socket_read,
	.write = socket_write,
	.poll = socket_poll,
	.close = socket_close
};

int
unix_socket_create(struct fd_table *table, int domain, int type, int protocol)
{
	struct os_credentials credentials = {0};
	if (table == NULL)
		return -EINVAL;
	if (domain != (int)UNIX_SOCKET_AF_UNIX)
		return -EAFNOSUPPORT;
	if (type != (int)UNIX_SOCKET_STREAM || protocol != 0)
		return -EPROTONOSUPPORT;
	(void)process_current_credentials(&credentials);
	uint64_t flags = state_guard_enter();
	struct unix_socket_state *state = socket_allocate();
	if (state != NULL) {
		state->owner_pid = process_current_pid();
		state->owner_uid = credentials.effective_uid;
		state->owner_gid = credentials.effective_gid;
	}
	state_guard_leave(flags);
	if (state == NULL)
		return -ENOBUFS;
	struct fd_object *object = fd_object_create(&socket_operations, state);
	if (object == NULL) {
		flags = state_guard_enter();
		*state = (struct unix_socket_state){0};
		state_guard_leave(flags);
		return -ENOMEM;
	}
	object->status_flags = 2;
	int descriptor = fd_install(table, object);
	if (descriptor < 0)
		fd_object_put(object);
	return descriptor;
}

int
unix_socket_bind(struct fd_object *object,
	const struct unix_socket_address *address, size_t length)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (address == NULL || address->family != UNIX_SOCKET_AF_UNIX ||
	    length < sizeof(address->family) + 1 ||
	    length > sizeof(*address))
		return -EINVAL;
	size_t path_length = length - sizeof(address->family);
	if (path_length == 1 && address->path[0] == 0)
		return -EINVAL;
	if (state->bound || state->listener)
		return -EINVAL;
	uint64_t flags = state_guard_enter();
	for (size_t i = 0; i < UNIX_SOCKET_LIMIT; i++) {
		struct unix_socket_state *other = &sockets[i];
		if (other != state && other->used && other->bound &&
		    !other->closed && other->path_length == path_length) {
			bool equal = true;
			for (size_t j = 0; j < path_length; j++)
				if (other->path[j] != address->path[j])
					equal = false;
			if (equal) {
				state_guard_leave(flags);
				return -EADDRINUSE;
			}
		}
	}
	for (size_t i = 0; i < path_length; i++)
		state->path[i] = address->path[i];
	state->path_length = path_length;
	state->bound = true;
	state_guard_leave(flags);
	return 0;
}

int
unix_socket_listen(struct fd_object *object, int backlog)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (!state->bound || state->closed)
		return -EINVAL;
	(void)backlog;
	state->listener = true;
	return 0;
}

int
unix_socket_connect(struct fd_object *object,
	const struct unix_socket_address *address, size_t length)
{
	struct unix_socket_state *client = socket_state(object);
	struct unix_socket_state *listener = NULL;
	struct os_credentials credentials = {0};
	if (client == NULL)
		return -ENOTSOCK;
	if (address == NULL || address->family != UNIX_SOCKET_AF_UNIX ||
	    length < sizeof(address->family) + 1 || length > sizeof(*address))
		return -EINVAL;
	size_t path_length = length - sizeof(address->family);
	if (path_length == 1 && address->path[0] == 0)
		return -EINVAL;
	(void)process_current_credentials(&credentials);
	uint64_t flags = state_guard_enter();
	for (size_t i = 0; i < UNIX_SOCKET_LIMIT; i++) {
		struct unix_socket_state *candidate = &sockets[i];
		if (!candidate->used || !candidate->bound || !candidate->listener ||
		    candidate->closed || candidate->path_length != path_length)
			continue;
		bool equal = true;
		for (size_t j = 0; j < path_length; j++)
			if (candidate->path[j] != address->path[j])
				equal = false;
		if (equal) {
			listener = candidate;
			break;
		}
	}
	if (listener == NULL) {
		state_guard_leave(flags);
		return -ECONNREFUSED;
	}
	if (client->peer != NULL || client->listener || client->closed) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	if (listener->pending_count == UNIX_SOCKET_BACKLOG) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	struct unix_socket_state *server = socket_allocate();
	if (server == NULL) {
		state_guard_leave(flags);
		return -ENOBUFS;
	}
	client->owner_uid = credentials.effective_uid;
	client->owner_gid = credentials.effective_gid;
	server->peer = client;
	client->peer = server;
	server->connected = true;
	client->connected = true;
	server->owner_pid = listener->owner_pid;
	server->peer_pid = client->owner_pid;
	server->peer_uid = client->owner_uid;
	server->peer_gid = client->owner_gid;
	client->peer_pid = listener->owner_pid;
	client->peer_uid = listener->owner_uid;
	client->peer_gid = listener->owner_gid;
	size_t tail = (listener->pending_head + listener->pending_count) %
	    UNIX_SOCKET_BACKLOG;
	listener->pending[tail] = server;
	listener->pending_count++;
	process_wake_channel(&listener->read_wait);
	fd_readiness_wake();
	state_guard_leave(flags);
	return 0;
}

int
unix_socket_accept(struct fd_table *table, struct fd_object *object)
{
	struct unix_socket_state *listener = socket_state(object);
	if (table == NULL || listener == NULL)
		return -ENOTSOCK;
	uint64_t flags = state_guard_enter();
	if (!listener->listener || listener->closed) {
		state_guard_leave(flags);
		return -EINVAL;
	}
	if (listener->pending_count == 0) {
		state_guard_leave(flags);
		return -EAGAIN;
	}
	struct unix_socket_state *state = listener->pending[
	    listener->pending_head];
	listener->pending[listener->pending_head] = NULL;
	listener->pending_head = (listener->pending_head + 1) %
	    UNIX_SOCKET_BACKLOG;
	listener->pending_count--;
	fd_readiness_wake();
	state_guard_leave(flags);
	struct fd_object *accepted = fd_object_create(&socket_operations, state);
	if (accepted == NULL) {
		socket_close(state);
		return -ENOMEM;
	}
	accepted->status_flags = 2;
	int descriptor = fd_install(table, accepted);
	if (descriptor < 0)
		fd_object_put(accepted);
	return descriptor;
}

int
unix_socket_shutdown(struct fd_object *object, int how)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (how < UNIX_SOCKET_SHUT_RD || how > UNIX_SOCKET_SHUT_RDWR)
		return -EINVAL;
	uint64_t flags = state_guard_enter();
	if (!state->connected || state->listener) {
		state_guard_leave(flags);
		return -ENOTCONN;
	}
	if (how == UNIX_SOCKET_SHUT_RD || how == UNIX_SOCKET_SHUT_RDWR) {
		state->read_shutdown = true;
		state->head = 0;
		state->count = 0;
		if (state->peer != NULL)
			process_wake_channel(&state->peer->write_wait);
	}
	if (how == UNIX_SOCKET_SHUT_WR || how == UNIX_SOCKET_SHUT_RDWR) {
		state->write_shutdown = true;
		if (state->peer != NULL)
			process_wake_channel(&state->peer->read_wait);
	}
	fd_readiness_wake();
	state_guard_leave(flags);
	return 0;
}

int
unix_socket_get_option(struct fd_object *object, int option, void *value,
	size_t *length)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (value == NULL || length == NULL)
		return -EINVAL;
	uint64_t flags = state_guard_enter();
	if (option == UNIX_SOCKET_SO_PEERCRED) {
		struct os_unix_ucred credential;
		if (!state->connected || state->peer == NULL) {
			state_guard_leave(flags);
			return -ENOTCONN;
		}
		if (*length < sizeof(credential)) {
			state_guard_leave(flags);
			return -EINVAL;
		}
		credential = (struct os_unix_ucred){
			.pid = (int32_t)state->peer_pid,
			.uid = state->peer_uid,
			.gid = state->peer_gid
		};
		*(struct os_unix_ucred *)value = credential;
		*length = sizeof(credential);
	} else if (option == UNIX_SOCKET_SO_SNDBUF ||
	    option == UNIX_SOCKET_SO_RCVBUF) {
		if (*length < sizeof(int)) {
			state_guard_leave(flags);
			return -EINVAL;
		}
		*(int *)value = (int)(option == UNIX_SOCKET_SO_SNDBUF ?
		    state->send_capacity : state->receive_capacity);
		*length = sizeof(int);
	} else {
		state_guard_leave(flags);
		return -EOPNOTSUPP;
	}
	state_guard_leave(flags);
	return 0;
}

int
unix_socket_set_option(struct fd_object *object, int option,
	const void *value, size_t length)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (option == UNIX_SOCKET_SO_REUSEADDR ||
	    option == UNIX_SOCKET_SO_KEEPALIVE) {
		if (value == NULL || length != sizeof(int))
			return -EINVAL;
		return 0;
	}
	if (option == UNIX_SOCKET_SO_SNDBUF ||
	    option == UNIX_SOCKET_SO_RCVBUF) {
		if (value == NULL || length != sizeof(int))
			return -EINVAL;
		int capacity = *(const int *)value;
		if (capacity < 1024 || capacity > (int)UNIX_SOCKET_BUFFER_SIZE)
			return -EINVAL;
		uint64_t flags = state_guard_enter();
		if (option == UNIX_SOCKET_SO_SNDBUF)
			state->send_capacity = (size_t)capacity;
		else
			state->receive_capacity = (size_t)capacity;
		fd_readiness_wake();
		state_guard_leave(flags);
		return 0;
	}
	return -EOPNOTSUPP;
}

int
unix_socket_bytes_available(struct fd_object *object, size_t *available)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return -ENOTSOCK;
	if (available == NULL)
		return -EINVAL;
	uint64_t flags = state_guard_enter();
	*available = state->count;
	state_guard_leave(flags);
	return 0;
}

bool
unix_socket_is_object(const struct fd_object *object)
{
	return object != NULL && object->operations == &socket_operations;
}

void *
unix_socket_wait_channel(struct fd_object *object, bool reading)
{
	struct unix_socket_state *state = socket_state(object);
	if (state == NULL)
		return NULL;
	return reading ? (void *)&state->read_wait : (void *)&state->write_wait;
}
