#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kernel/errno.h"
#include "kernel/fd.h"
#include "kernel/pty.h"

struct test_termios {
	uint32_t input_flags, output_flags, control_flags, local_flags;
	uint8_t control_chars[32];
	uint32_t input_speed, output_speed;
};

static unsigned allocations;
static unsigned fail_at;
static unsigned calls;

void *kheap_alloc(size_t size)
{
	if (++calls == fail_at)
		return NULL;
	void *result = malloc(size);
	if (result != NULL)
		allocations++;
	return result;
}
bool kheap_free(void *pointer)
{
	if (pointer != NULL) { assert(allocations != 0); allocations--; }
	free(pointer);
	return true;
}
void klog(const char *message) { fputs(message, stderr); }
void klog_u64(uint64_t value) { (void)value; }
_Noreturn void kernel_halt(void) { abort(); }
void process_wake_channel(void *channel) { (void)channel; }
bool serial_write(const char *buffer, size_t length)
{ (void)buffer; (void)length; abort(); }
bool tty_write(const void *buffer, size_t length)
{ (void)buffer; (void)length; abort(); }
int tty_read(void *buffer, size_t length, size_t *amount)
{ (void)buffer; (void)length; (void)amount; abort(); }
bool terminal_input_available(void) { abort(); }

int main(void)
{
	struct fd_table table = {0};
	int master, slave;
	char buffer[8192];
	size_t amount;
	uint16_t dimensions[4] = {40, 100, 800, 600};

	for (fail_at = 1; fail_at <= 5; fail_at++) {
		calls = 0;
		assert(pty_create(&table, &master, &slave) == -ENOMEM);
		assert(allocations == 0);
	}
	fail_at = 0;
	assert(pty_create(&table, &master, &slave) == 0);
	struct fd_object *m = table.entries[master], *s = table.entries[slave];
	int duplicated = fd_dup(&table, slave);
	assert(duplicated == 0 && table.entries[duplicated] == s);
	assert(fd_close(&table, duplicated) == 0);
	assert(fd_object_is_terminal(s));
	assert(fd_read(&table, slave, buffer, sizeof(buffer), &amount) == -EAGAIN);
	assert(fd_write(&table, master, "ab\bc\n", 5, &amount) == 0 && amount == 5);
	assert(fd_read(&table, slave, buffer, 1, &amount) == 0 && amount == 1 && buffer[0] == 'a');
	assert(fd_read(&table, slave, buffer, sizeof(buffer), &amount) == 0 && amount == 2);
	assert(memcmp(buffer, "c\n", 2) == 0);
	assert(fd_read(&table, master, buffer, sizeof(buffer), &amount) == 0 && amount == 7);
	assert(memcmp(buffer, "ab\b \bc\n", 7) == 0);
	assert(fd_write(&table, master, "\4", 1, &amount) == 0);
	assert(fd_read(&table, slave, buffer, sizeof(buffer), &amount) == 0 && amount == 0);
	assert(pty_set_raw(s, true) == 0);
	memset(buffer, 'x', sizeof(buffer));
	assert(fd_write(&table, master, buffer, sizeof(buffer), &amount) == 0 && amount == 4096);
	assert(fd_write(&table, master, buffer, 1, &amount) == -EAGAIN);
	assert(fd_object_poll(s, FD_POLL_READ) == FD_POLL_READ);
	assert(fd_object_poll(m, FD_POLL_WRITE) == 0);
	assert(fd_read(&table, slave, buffer, sizeof(buffer), &amount) == 0 && amount == 4096);
	assert(fd_object_poll(m, FD_POLL_WRITE) == FD_POLL_WRITE);
	assert(pty_window(s, dimensions, true) == 0);
	memset(dimensions, 0, sizeof(dimensions));
	assert(pty_window(m, dimensions, false) == 0 && dimensions[0] == 40 && dimensions[1] == 100);
	struct test_termios termios;
	assert(pty_set_raw(s, false) == 0);
	assert(pty_termios(s, &termios, false) == 0);
	assert((termios.local_flags & 0x2U) != 0 && (termios.local_flags & 0x8U) != 0);
	termios.local_flags &= ~0x8U;
	assert(pty_termios(s, &termios, true) == 0);
	assert(fd_write(&table, master, "q\n", 2, &amount) == 0 && amount == 2);
	assert(fd_read(&table, master, buffer, sizeof(buffer), &amount) == -EAGAIN);
	assert(fd_read(&table, slave, buffer, sizeof(buffer), &amount) == 0 && amount == 2);
	assert(memcmp(buffer, "q\n", 2) == 0);
	assert(pty_termios(s, &termios, false) == 0);
	termios.local_flags &= ~0x2U;
	assert(pty_termios(s, &termios, true) == 0);
	assert(fd_write(&table, master, "raw", 3, &amount) == 0 && amount == 3);
	assert(fd_read(&table, slave, buffer, sizeof(buffer), &amount) == 0 && amount == 3);
	assert(memcmp(buffer, "raw", 3) == 0);
	assert(pty_set_raw(s, false) == 0);
	assert(fd_dup2(&table, slave, 10) == 10);
	assert(fd_close(&table, slave) == 0);
	assert(fd_write(&table, 10, "reply", 5, &amount) == 0);
	assert(fd_close(&table, 10) == 0);
	assert(fd_read(&table, master, buffer, sizeof(buffer), &amount) == 0 && amount == 5);
	assert(memcmp(buffer, "reply", 5) == 0);
	assert(fd_read(&table, master, buffer, sizeof(buffer), &amount) == 0 && amount == 0);
	assert(fd_write(&table, master, "x", 1, &amount) == -EIO);
	assert(fd_close(&table, master) == 0 && allocations == 0);
	/* A full descriptor table must not leak a newly allocated PTY. */
	for (unsigned i = 3; i < FD_TABLE_CAPACITY; i++) table.entries[i] = (void *)1;
	assert(pty_create(&table, &master, &slave) == -EMFILE && allocations == 0);
	puts("PTY host tests passed");
	return 0;
}
