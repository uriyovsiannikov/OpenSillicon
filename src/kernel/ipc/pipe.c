#include <stddef.h>

#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/process.h"

struct pipe_state {
	struct pipe_buffer buffer;
	uint32_t readers;
	uint32_t writers;
	uint8_t read_wait_channel;
	uint8_t write_wait_channel;
};

enum pipe_end {
	PIPE_END_READ,
	PIPE_END_WRITE
};

struct pipe_endpoint {
	struct pipe_state *pipe;
	enum pipe_end end;
};

static int pipe_object_read(void *, uint64_t, void *, size_t, size_t *);
static int pipe_object_write(void *, uint64_t, const void *, size_t, size_t *);
static void pipe_read_close(void *);
static void pipe_write_close(void *);
static uint32_t pipe_read_poll(void *, uint32_t);
static uint32_t pipe_write_poll(void *, uint32_t);

static const struct fd_operations pipe_read_operations = {
	.read = pipe_object_read,
	.poll = pipe_read_poll,
	.close = pipe_read_close
};

static const struct fd_operations pipe_write_operations = {
	.write = pipe_object_write,
	.poll = pipe_write_poll,
	.close = pipe_write_close
};

static void
pipe_free(void *allocation)
{
	if (!kheap_free(allocation)) {
		klog("OpenSillicon panic: cannot release pipe allocation\n");
		kernel_halt();
	}
}

static void
pipe_release_if_unused(struct pipe_state *pipe)
{
	if (pipe->readers == 0 && pipe->writers == 0)
		pipe_free(pipe);
}

static void
pipe_read_close(void *context)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;

	if (pipe->readers != 0)
		pipe->readers--;
	process_wake_channel(&pipe->write_wait_channel);
	fd_readiness_wake();
	pipe_free(endpoint);
	pipe_release_if_unused(pipe);
}

static void
pipe_write_close(void *context)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;

	if (pipe->writers != 0)
		pipe->writers--;
	process_wake_channel(&pipe->read_wait_channel);
	fd_readiness_wake();
	pipe_free(endpoint);
	pipe_release_if_unused(pipe);
}

static int
pipe_object_read(void *context, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;

	(void)offset;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*read_length = 0;
	if (length == 0)
		return 0;
	if (pipe->buffer.count == 0)
		return pipe->writers == 0 ? 0 : -EAGAIN;
	*read_length = pipe_buffer_peek(&pipe->buffer, buffer, length);
	if (pipe_buffer_consume(&pipe->buffer, *read_length) != 0)
		return -EINVAL;
	process_wake_channel(&pipe->write_wait_channel);
	fd_readiness_wake();
	return 0;
}

static int
pipe_object_write(void *context, uint64_t offset, const void *buffer,
	size_t length, size_t *write_length)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;

	(void)offset;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*write_length = 0;
	if (length == 0)
		return 0;
	if (pipe->readers == 0)
		return -EPIPE;
	if (pipe->buffer.count == PIPE_CAPACITY)
		return -EAGAIN;
	*write_length = pipe_buffer_write(&pipe->buffer, buffer, length);
	process_wake_channel(&pipe->read_wait_channel);
	fd_readiness_wake();
	return 0;
}

int
pipe_create(struct fd_table *table, int *read_descriptor,
	int *write_descriptor)
{
	struct pipe_state *pipe;
	struct pipe_endpoint *read_endpoint;
	struct pipe_endpoint *write_endpoint;
	struct fd_object *read_object;
	struct fd_object *write_object;
	int result;

	if (table == NULL || read_descriptor == NULL || write_descriptor == NULL)
		return -EINVAL;
	pipe = kheap_alloc(sizeof(*pipe));
	read_endpoint = kheap_alloc(sizeof(*read_endpoint));
	write_endpoint = kheap_alloc(sizeof(*write_endpoint));
	if (pipe == NULL || read_endpoint == NULL || write_endpoint == NULL) {
		if (pipe != NULL)
			pipe_free(pipe);
		if (read_endpoint != NULL)
			pipe_free(read_endpoint);
		if (write_endpoint != NULL)
			pipe_free(write_endpoint);
		return -ENOMEM;
	}
	pipe_buffer_init(&pipe->buffer);
	pipe->readers = 1;
	pipe->writers = 1;
	pipe->read_wait_channel = 0;
	pipe->write_wait_channel = 0;
	read_endpoint->pipe = pipe;
	read_endpoint->end = PIPE_END_READ;
	write_endpoint->pipe = pipe;
	write_endpoint->end = PIPE_END_WRITE;
	read_object = fd_object_create(&pipe_read_operations, read_endpoint);
	if (read_object == NULL) {
		pipe_free(read_endpoint);
		pipe_free(write_endpoint);
		pipe_free(pipe);
		return -ENOMEM;
	}
	read_object->status_flags = 0;
	write_object = fd_object_create(&pipe_write_operations, write_endpoint);
	if (write_object == NULL) {
		pipe_free(write_endpoint);
		fd_object_put(read_object);
		pipe->writers = 0;
		pipe_release_if_unused(pipe);
		return -ENOMEM;
	}
	write_object->status_flags = 1;
	result = fd_install_pair(table, read_object, write_object,
	    read_descriptor, write_descriptor);
	if (result != 0) {
		fd_object_put(read_object);
		fd_object_put(write_object);
	}
	return result;
}

bool
pipe_is_read_endpoint(const struct fd_object *object)
{
	return object != NULL && object->operations == &pipe_read_operations;
}

bool
pipe_is_write_endpoint(const struct fd_object *object)
{
	return object != NULL && object->operations == &pipe_write_operations;
}

int
pipe_read_stage(struct fd_object *object, void *buffer, size_t length,
	size_t *read_length)
{
	struct pipe_endpoint *endpoint;
	struct pipe_state *pipe;

	if (!pipe_is_read_endpoint(object))
		return -EBADF;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*read_length = 0;
	if (length == 0)
		return 0;
	endpoint = object->context;
	pipe = endpoint->pipe;
	if (pipe->buffer.count == 0)
		return pipe->writers == 0 ? 0 : -EAGAIN;
	*read_length = pipe_buffer_peek(&pipe->buffer, buffer, length);
	return 0;
}

int
pipe_read_commit(struct fd_object *object, size_t length)
{
	struct pipe_endpoint *endpoint;
	struct pipe_state *pipe;
	int result;

	if (!pipe_is_read_endpoint(object))
		return -EBADF;
	endpoint = object->context;
	pipe = endpoint->pipe;
	result = pipe_buffer_consume(&pipe->buffer, length);
	if (result == 0 && length != 0)
		process_wake_channel(&pipe->write_wait_channel);
	if (result == 0 && length != 0)
		fd_readiness_wake();
	return result == 0 ? 0 : -EINVAL;
}

int
pipe_write_try(struct fd_object *object, const void *buffer, size_t length,
	size_t *write_length)
{
	struct pipe_endpoint *endpoint;
	struct pipe_state *pipe;

	if (!pipe_is_write_endpoint(object))
		return -EBADF;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*write_length = 0;
	if (length == 0)
		return 0;
	endpoint = object->context;
	pipe = endpoint->pipe;
	if (pipe->readers == 0)
		return -EPIPE;
	if (pipe->buffer.count == PIPE_CAPACITY)
		return -EAGAIN;
	*write_length = pipe_buffer_write(&pipe->buffer, buffer, length);
	if (*write_length != 0) {
		process_wake_channel(&pipe->read_wait_channel);
		fd_readiness_wake();
	}
	return 0;
}

void *
pipe_wait_channel(struct fd_object *object, unsigned int operation)
{
	struct pipe_endpoint *endpoint;
	struct pipe_state *pipe;

	if (object == NULL)
		return NULL;
	endpoint = object->context;
	pipe = endpoint->pipe;
	if ((operation == PIPE_READ_WAIT &&
	    endpoint->end == PIPE_END_READ) ||
	    (operation == PIPE_WRITE_WAIT && endpoint->end == PIPE_END_WRITE))
		return operation == PIPE_READ_WAIT ?
		    (void *)&pipe->read_wait_channel :
		    (void *)&pipe->write_wait_channel;
	return NULL;
}

static uint32_t
pipe_read_poll(void *context, uint32_t events)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;
	uint32_t ready = 0;
	if (endpoint->end == PIPE_END_READ && (events & FD_POLL_READ) != 0 &&
	    (pipe->buffer.count != 0 || pipe->writers == 0))
		ready |= FD_POLL_READ;
	return ready;
}

static uint32_t
pipe_write_poll(void *context, uint32_t events)
{
	struct pipe_endpoint *endpoint = context;
	struct pipe_state *pipe = endpoint->pipe;
	uint32_t ready = 0;
	if (endpoint->end == PIPE_END_WRITE && (events & FD_POLL_WRITE) != 0 &&
	    (pipe->readers == 0 || pipe->buffer.count < PIPE_CAPACITY))
		ready |= FD_POLL_WRITE;
	if (endpoint->end == PIPE_END_WRITE && (events & FD_POLL_ERROR) != 0 &&
	    pipe->readers == 0)
		ready |= FD_POLL_ERROR;
	return ready;
}
