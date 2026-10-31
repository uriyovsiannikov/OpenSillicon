#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/fd.h"
#include "kernel/log.h"
#include "kernel/pipe.h"
#include "kernel/tty.h"

static unsigned int allocations;
static unsigned int frees;
static unsigned int allocation_calls;
static unsigned int fail_allocation;
static unsigned int wakeups;

void *
kheap_alloc(size_t size)
{
	allocation_calls++;
	if (fail_allocation != 0 && allocation_calls == fail_allocation)
		return NULL;
	allocations++;
	return calloc(1, size);
}

bool
kheap_free(void *pointer)
{
	if (pointer != NULL) {
		frees++;
		free(pointer);
	}
	return true;
}

void
process_wake_channel(void *channel)
{
	if (channel != NULL)
		wakeups++;
}

void klog(const char *message) { (void)message; }
_Noreturn void kernel_halt(void) { abort(); }
bool serial_write(const char *buffer, size_t length)
{ (void)buffer; (void)length; return true; }
bool tty_write(const void *buffer, size_t length)
{ (void)buffer; (void)length; return true; }
int tty_read(void *buffer, size_t length, size_t *read_length)
{ (void)buffer; (void)length; *read_length = 0; return -5; }

static void
test_ring_buffer(void)
{
	struct pipe_buffer buffer;
	uint8_t source[PIPE_CAPACITY];
	uint8_t output[PIPE_CAPACITY];
	size_t i;

	for (i = 0; i < sizeof(source); i++)
		source[i] = (uint8_t)i;
	pipe_buffer_init(&buffer);
	assert(pipe_buffer_count(&buffer) == 0);
	assert(pipe_buffer_peek(&buffer, output, 1) == 0);
	assert(pipe_buffer_write(&buffer, source, sizeof(source)) == PIPE_CAPACITY);
	assert(pipe_buffer_count(&buffer) == PIPE_CAPACITY);
	assert(pipe_buffer_write(&buffer, source, 1) == 0);
	assert(pipe_buffer_peek(&buffer, output, sizeof(output)) == PIPE_CAPACITY);
	assert(pipe_buffer_consume(&buffer, 3000) == 0);
	assert(pipe_buffer_write(&buffer, source, 3000) == 3000);
	assert(pipe_buffer_count(&buffer) == PIPE_CAPACITY);
	assert(pipe_buffer_peek(&buffer, output, sizeof(output)) == PIPE_CAPACITY);
	for (i = 0; i < PIPE_CAPACITY - 3000; i++)
		assert(output[i] == (uint8_t)(i + 3000));
	assert(pipe_buffer_consume(&buffer, 10) == 0);
	assert(pipe_buffer_write(&buffer, source, 20) == 10);
	assert(pipe_buffer_peek(&buffer, output, 3) == 3);
	assert(pipe_buffer_consume(&buffer, PIPE_CAPACITY) == 0);
	assert(pipe_buffer_count(&buffer) == 0);
	assert(pipe_buffer_write(&buffer, source, 3) == 3);
	assert(pipe_buffer_peek(&buffer, output, 8) == 3);
	assert(output[0] == source[0] && output[2] == source[2]);
	assert(pipe_buffer_consume(&buffer, 3) == 0);
}

static void
test_pipe_lifetime_and_transactions(void)
{
	struct fd_table table = {0};
	struct fd_object *reader;
	struct fd_object *writer;
	int read_fd;
	int write_fd;
	char staged[16];
	char output[16];
	size_t amount;
	unsigned int wake_before;

	assert(pipe_create(&table, &read_fd, &write_fd) == 0);
	reader = table.entries[read_fd];
	writer = table.entries[write_fd];
	assert(pipe_write_try(writer, "hello", 5, &amount) == 0 && amount == 5);
	assert(pipe_read_stage(reader, staged, sizeof(staged), &amount) == 0 &&
	    amount == 5);
	/* Simulate copy-out failure: no commit, so a retry sees the same bytes. */
	assert(pipe_read_stage(reader, output, sizeof(output), &amount) == 0 &&
	    amount == 5);
	assert(output[0] == 'h' && output[4] == 'o');
	assert(pipe_read_commit(reader, amount) == 0);
	assert(pipe_read_stage(reader, output, sizeof(output), &amount) == -11);

	fd_object_get(reader);
	assert(fd_close(&table, (uint64_t)read_fd) == 0);
	assert(pipe_write_try(writer, "x", 1, &amount) == 0 && amount == 1);
	wake_before = wakeups;
	fd_object_put(reader);
	assert(wakeups > wake_before);
	assert(pipe_write_try(writer, "x", 1, &amount) == -32);
	assert(fd_close(&table, (uint64_t)write_fd) == 0);
}

static void
test_writer_reference_lifetime(void)
{
	struct fd_table table = {0};
	struct fd_object *reader;
	struct fd_object *writer;
	int read_fd;
	int write_fd;
	char byte;
	size_t amount;

	assert(pipe_create(&table, &read_fd, &write_fd) == 0);
	reader = table.entries[read_fd];
	writer = table.entries[write_fd];
	fd_object_get(writer);
	assert(fd_close(&table, (uint64_t)write_fd) == 0);
	assert(pipe_read_stage(reader, &byte, 1, &amount) == -11);
	fd_object_put(writer);
	assert(pipe_read_stage(reader, &byte, 1, &amount) == 0 && amount == 0);
	assert(fd_close(&table, (uint64_t)read_fd) == 0);
}

static void
test_failure_cleanup(void)
{
	struct fd_table table = {0};
	int read_fd;
	int write_fd;
	unsigned int i;

	for (i = 1; i <= 5; i++) {
		fail_allocation = allocation_calls + i;
		assert(pipe_create(&table, &read_fd, &write_fd) != 0);
		assert(table.entries[3] == NULL && table.entries[4] == NULL);
		assert(allocations == frees);
	}
	fail_allocation = 0;
	for (i = 3; i < FD_TABLE_CAPACITY; i++)
		table.entries[i] = (struct fd_object *)(uintptr_t)1;
	assert(pipe_create(&table, &read_fd, &write_fd) == -24);
	assert(allocations == frees);
}

int
main(void)
{
	test_ring_buffer();
	test_pipe_lifetime_and_transactions();
	test_writer_reference_lifetime();
	test_failure_cleanup();
	puts("anonymous pipe host tests passed");
	return 0;
}
