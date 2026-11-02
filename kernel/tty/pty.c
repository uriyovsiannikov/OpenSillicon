#include "kernel/errno.h"
#include "kernel/heap.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/pty.h"

struct pty_state {
	struct pipe_buffer input;
	struct pipe_buffer output;
	uint8_t line[256];
	size_t line_length;
	bool master_open;
	bool slave_open;
	bool raw;
	bool echo;
	bool output_post;
	bool output_newline_crlf;
	bool eof;
	uint16_t dimensions[4];
};

struct pty_endpoint {
	struct pty_state *pty;
	bool master;
};

static void
pty_free(void *pointer)
{
	if (!kheap_free(pointer)) {
		klog("OpenSillicon panic: cannot release PTY allocation\n");
		kernel_halt();
	}
}

static int
pty_read(void *context, uint64_t offset, void *buffer, size_t length,
	size_t *read_length)
{
	struct pty_endpoint *endpoint = context;
	struct pty_state *pty = endpoint->pty;
	struct pipe_buffer *queue = endpoint->master ? &pty->output : &pty->input;
	bool peer_open = endpoint->master ? pty->slave_open : pty->master_open;

	(void)offset;
	if (read_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*read_length = 0;
	if (length == 0)
		return 0;
	if (queue->count == 0) {
		if (!endpoint->master && pty->eof) {
			pty->eof = false;
			return 0;
		}
		return peer_open ? -EAGAIN : 0;
	}
	*read_length = pipe_buffer_peek(queue, buffer, length);
	(void)pipe_buffer_consume(queue, *read_length);
	fd_readiness_wake();
	return 0;
}

static int
pty_write(void *context, uint64_t offset, const void *buffer, size_t length,
	size_t *write_length)
{
	struct pty_endpoint *endpoint = context;
	struct pty_state *pty = endpoint->pty;
	const uint8_t *bytes = buffer;

	(void)offset;
	if (write_length == NULL || (buffer == NULL && length != 0))
		return -EINVAL;
	*write_length = 0;
	if (length == 0)
		return 0;
	if (!(endpoint->master ? pty->slave_open : pty->master_open))
		return -EIO;
	if (endpoint->master && !pty->raw && pty->eof)
		return -EAGAIN;
	if (!endpoint->master || pty->raw) {
		struct pipe_buffer *queue = endpoint->master ? &pty->input : &pty->output;
		if (endpoint->master || !pty->output_post || !pty->output_newline_crlf) {
			*write_length = pipe_buffer_write(queue, buffer, length);
		} else {
			while (*write_length < length) {
				uint8_t byte = bytes[*write_length];
				if (byte == '\n') {
					if (PIPE_CAPACITY - queue->count < 2)
						break;
					(void)pipe_buffer_write(queue, "\r\n", 2);
				} else if (pipe_buffer_write(queue, &byte, 1) != 1) {
					break;
				}
				(*write_length)++;
			}
		}
	} else {
		/* Canonical input: reserve capacity before changing a line or echoing
		 * a byte, so a short write can be retried without duplicating input. */
		while (*write_length < length) {
			uint8_t byte = bytes[*write_length];
			if (byte == '\r')
				byte = '\n';
			if (byte == 4) {
				if (pty->line_length > PIPE_CAPACITY - pty->input.count)
					break;
				if (pty->line_length == 0)
					pty->eof = true;
				else
					(void)pipe_buffer_write(&pty->input, pty->line,
					    pty->line_length);
				pty->line_length = 0;
			} else if (byte == 8 || byte == 127) {
				if (pty->line_length != 0) {
					if (pty->echo && PIPE_CAPACITY - pty->output.count < 3)
						break;
					pty->line_length--;
					if (pty->echo)
						(void)pipe_buffer_write(&pty->output, "\b \b", 3);
				}
			} else {
				if ((pty->echo && pty->output.count == PIPE_CAPACITY) ||
				    (byte != '\n' && pty->line_length == sizeof(pty->line) - 1))
					break;
				if (byte == '\n' && pty->line_length + 1 >
				    PIPE_CAPACITY - pty->input.count)
					break;
				pty->line[pty->line_length++] = byte;
				if (pty->echo)
					(void)pipe_buffer_write(&pty->output, &byte, 1);
				if (byte == '\n') {
					(void)pipe_buffer_write(&pty->input, pty->line,
					    pty->line_length);
					pty->line_length = 0;
				}
			}
			(*write_length)++;
		}
	}
	fd_readiness_wake();
	return *write_length != 0 ? 0 : -EAGAIN;
}

static uint32_t
pty_poll(void *context, uint32_t events)
{
	struct pty_endpoint *endpoint = context;
	struct pty_state *pty = endpoint->pty;
	struct pipe_buffer *read_queue = endpoint->master ? &pty->output : &pty->input;
	struct pipe_buffer *write_queue = endpoint->master ? &pty->input : &pty->output;
	bool peer_open = endpoint->master ? pty->slave_open : pty->master_open;
	uint32_t ready = 0;

	if (read_queue->count != 0 || !peer_open || (!endpoint->master && pty->eof))
		ready |= FD_POLL_READ;
	if (!peer_open)
		ready |= FD_POLL_ERROR | FD_POLL_WRITE;
	else if (write_queue->count < PIPE_CAPACITY &&
	    (!endpoint->master || pty->raw || !pty->eof) &&
	    (!endpoint->master || pty->raw ||
	    (pty->line_length < sizeof(pty->line) - 1 && pty->output.count < PIPE_CAPACITY)))
		ready |= FD_POLL_WRITE;
	return ready & events;
}

static bool
pty_terminal(void *context)
{
	(void)context;
	return true;
}

static int
pty_stat(void *context, struct fd_metadata *metadata)
{
	(void)context;
	if (metadata == NULL)
		return -EINVAL;
	*metadata = (struct fd_metadata){.mode = 0020600, .type = 3};
	return 0;
}

static int
pty_seek(void *context, uint64_t current, int64_t offset, int whence,
	uint64_t *result)
{
	(void)context;
	(void)current;
	(void)offset;
	(void)whence;
	(void)result;
	return -ESPIPE;
}

static void
pty_close(void *context)
{
	struct pty_endpoint *endpoint = context;
	struct pty_state *pty = endpoint->pty;

	if (endpoint->master)
		pty->master_open = false;
	else
		pty->slave_open = false;
	fd_readiness_wake();
	pty_free(endpoint);
	if (!pty->master_open && !pty->slave_open)
		pty_free(pty);
}

static const struct fd_operations pty_operations = {
	.read = pty_read,
	.write = pty_write,
	.poll = pty_poll,
	.is_terminal = pty_terminal,
	.stat = pty_stat,
	.seek = pty_seek,
	.close = pty_close
};

bool
pty_is_object(const struct fd_object *object)
{
	return object != NULL && object->operations == &pty_operations;
}

int
pty_bytes_available(struct fd_object *object, size_t *available)
{
	struct pty_endpoint *endpoint;
	if (!pty_is_object(object) || available == NULL)
		return -EINVAL;
	endpoint = object->context;
	if (endpoint->master && !endpoint->pty->raw && endpoint->pty->eof)
		return -EAGAIN;
	*available = endpoint->master ? endpoint->pty->output.count : endpoint->pty->input.count;
	return 0;
}

int
pty_window(struct fd_object *object, uint16_t dimensions[4], bool set)
{
	struct pty_endpoint *endpoint;
	if (!pty_is_object(object) || dimensions == NULL)
		return -EINVAL;
	endpoint = object->context;
	for (unsigned int i = 0; i < 4; i++) {
		if (set)
			endpoint->pty->dimensions[i] = dimensions[i];
		else
			dimensions[i] = endpoint->pty->dimensions[i];
	}
	return 0;
}

int
pty_set_raw(struct fd_object *object, bool enabled)
{
	struct pty_endpoint *endpoint;

	if (!pty_is_object(object))
		return -ENOTTY;
	endpoint = object->context;
	if (endpoint->pty->raw != enabled) {
		endpoint->pty->raw = enabled;
	}
	return 0;
}

/* ABI-compatible x86-64 termios subset used by the userland X clients. */
struct pty_termios_abi {
	uint32_t input_flags;
	uint32_t output_flags;
	uint32_t control_flags;
	uint32_t local_flags;
	uint8_t control_chars[32];
	uint32_t input_speed;
	uint32_t output_speed;
};

int
pty_termios(struct fd_object *object, void *attributes, bool set)
{
	struct pty_endpoint *endpoint;
	struct pty_termios_abi *termios = attributes;
	const uint32_t canonical = 0x0002U;
	const uint32_t echo = 0x0008U;

	if (!pty_is_object(object) || termios == NULL)
		return -EINVAL;
	endpoint = object->context;
	if (set) {
		endpoint->pty->raw = (termios->local_flags & canonical) == 0;
		endpoint->pty->echo = (termios->local_flags & echo) != 0;
		endpoint->pty->output_post = (termios->output_flags & 0x1U) != 0;
		endpoint->pty->output_newline_crlf = (termios->output_flags & 0x4U) != 0;
	} else {
		*termios = (struct pty_termios_abi){
			.input_flags = 0x00000100U,
			.output_flags = 0x00000005U,
			.control_flags = 0x000000bFU,
			.local_flags = 0x00008A3BU,
			.input_speed = 38400,
			.output_speed = 38400
		};
		termios->control_chars[0] = 3;
		termios->control_chars[1] = 28;
		termios->control_chars[2] = 127;
		termios->control_chars[3] = 21;
		termios->control_chars[4] = 4;
		termios->control_chars[5] = 0;
		termios->control_chars[6] = 1;
		termios->control_chars[9] = 19;
		termios->control_chars[10] = 17;
		termios->control_chars[16] = 26;
		if (!endpoint->pty->raw)
			termios->local_flags |= canonical;
		else
			termios->local_flags &= ~canonical;
		if (!endpoint->pty->echo)
			termios->local_flags &= ~echo;
	}
	return 0;
}

int
pty_create(struct fd_table *table, int *master, int *slave)
{
	struct pty_state *pty = kheap_alloc(sizeof(*pty));
	struct pty_endpoint *first = kheap_alloc(sizeof(*first));
	struct pty_endpoint *second = kheap_alloc(sizeof(*second));
	struct fd_object *master_object;
	struct fd_object *slave_object;
	int result;

	if (pty == NULL || first == NULL || second == NULL) {
		pty_free(pty);
		pty_free(first);
		pty_free(second);
		return -ENOMEM;
	}
	*pty = (struct pty_state){.master_open = true, .slave_open = true,
	    .echo = true, .output_post = true, .output_newline_crlf = true,
	    .dimensions = {24, 80, 0, 0}};
	*first = (struct pty_endpoint){.pty = pty, .master = true};
	*second = (struct pty_endpoint){.pty = pty, .master = false};
	master_object = fd_object_create(&pty_operations, first);
	slave_object = fd_object_create(&pty_operations, second);
	if (master_object == NULL || slave_object == NULL) {
		/* Put each endpoint exactly once, including allocation failures. */
		if (master_object != NULL)
			fd_object_put(master_object);
		else
			pty_close(first);
		if (slave_object != NULL)
			fd_object_put(slave_object);
		else
			pty_close(second);
		return -ENOMEM;
	}
	result = fd_install_pair(table, master_object, slave_object, master, slave);
	if (result != 0) {
		fd_object_put(master_object);
		fd_object_put(slave_object);
	}
	return result;
}
