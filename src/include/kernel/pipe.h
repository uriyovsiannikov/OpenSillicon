#ifndef KERNEL_PIPE_H
#define KERNEL_PIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/fd.h"

#define PIPE_CAPACITY 4096U
#define PIPE_READ_WAIT 0U
#define PIPE_WRITE_WAIT 1U

struct pipe_buffer {
	uint8_t bytes[PIPE_CAPACITY];
	size_t head;
	size_t count;
};

void pipe_buffer_init(struct pipe_buffer *buffer);
size_t pipe_buffer_count(const struct pipe_buffer *buffer);
size_t pipe_buffer_write(struct pipe_buffer *buffer, const void *source,
	size_t length);
size_t pipe_buffer_peek(const struct pipe_buffer *buffer, void *destination,
	size_t length);
int pipe_buffer_consume(struct pipe_buffer *buffer, size_t length);

int pipe_create(struct fd_table *table, int *read_descriptor,
	int *write_descriptor);
bool pipe_is_read_endpoint(const struct fd_object *object);
bool pipe_is_write_endpoint(const struct fd_object *object);
int pipe_read_stage(struct fd_object *object, void *buffer, size_t length,
	size_t *read_length);
int pipe_read_commit(struct fd_object *object, size_t length);
int pipe_write_try(struct fd_object *object, const void *buffer,
	size_t length, size_t *write_length);
void *pipe_wait_channel(struct fd_object *object, unsigned int operation);

#endif
