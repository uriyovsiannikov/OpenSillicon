#include <stddef.h>
#include <stdint.h>

#include "kernel/pipe.h"

void
pipe_buffer_init(struct pipe_buffer *buffer)
{
	if (buffer == NULL)
		return;
	buffer->head = 0;
	buffer->count = 0;
}

size_t
pipe_buffer_count(const struct pipe_buffer *buffer)
{
	return buffer == NULL ? 0 : buffer->count;
}

size_t
pipe_buffer_write(struct pipe_buffer *buffer, const void *source,
	size_t length)
{
	const uint8_t *input = source;
	size_t amount;
	size_t tail;
	size_t i;

	if (buffer == NULL || (source == NULL && length != 0))
		return 0;
	amount = PIPE_CAPACITY - buffer->count;
	if (amount > length)
		amount = length;
	tail = (buffer->head + buffer->count) % PIPE_CAPACITY;
	for (i = 0; i < amount; i++)
		buffer->bytes[(tail + i) % PIPE_CAPACITY] = input[i];
	buffer->count += amount;
	return amount;
}

size_t
pipe_buffer_peek(const struct pipe_buffer *buffer, void *destination,
	size_t length)
{
	uint8_t *output = destination;
	size_t amount;
	size_t i;

	if (buffer == NULL || (destination == NULL && length != 0))
		return 0;
	amount = buffer->count < length ? buffer->count : length;
	for (i = 0; i < amount; i++)
		output[i] = buffer->bytes[(buffer->head + i) % PIPE_CAPACITY];
	return amount;
}

int
pipe_buffer_consume(struct pipe_buffer *buffer, size_t length)
{
	if (buffer == NULL || length > buffer->count)
		return -1;
	buffer->head = (buffer->head + length) % PIPE_CAPACITY;
	buffer->count -= length;
	return 0;
}
