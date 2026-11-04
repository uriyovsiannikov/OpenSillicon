#include <stdint.h>
#include <stddef.h>

#include "kernel/log.h"
#include "kernel/fd.h"
#include "kernel/spinlock.h"
#include "kernel/vfs.h"

#define COM1 0x3f8
#define BOOT_LOG_BUFFER_SIZE (64U * 1024U)

static char boot_log[BOOT_LOG_BUFFER_SIZE];
static size_t boot_log_length;
static bool boot_log_overflow;
static bool boot_log_flushed;
static struct spinlock log_lock = SPINLOCK_INITIALIZER;

static inline void
outb(uint16_t port, uint8_t value)
{
	__asm__ volatile ("outb %0, %1" : : "a" (value), "Nd" (port));
}

static inline uint8_t
inb(uint16_t port)
{
	uint8_t value;
	__asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

void
serial_init(void)
{
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x80);
	outb(COM1 + 0, 0x03);
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);
	outb(COM1 + 2, 0xc7);
	outb(COM1 + 4, 0x0b);
}

static void
serial_putc(char c)
{
	while ((inb(COM1 + 5) & 0x20) == 0)
		;
	outb(COM1, (uint8_t)c);
}

bool
serial_write(const char *buffer, size_t length)
{
	size_t i;

	if (buffer == NULL && length != 0)
		return false;
	for (i = 0; i < length; i++) {
		if (buffer[i] == '\n')
			serial_putc('\r');
		serial_putc(buffer[i]);
	}
	return true;
}

static void
log_write(const char *buffer, size_t length)
{
	uint64_t flags = spinlock_lock_irqsave(&log_lock);
	size_t i;

	for (i = 0; i < length; i++) {
		if (buffer[i] == '\n')
			serial_putc('\r');
		serial_putc(buffer[i]);
	}
	if (!boot_log_flushed) {
		if (length <= sizeof(boot_log) - boot_log_length) {
			for (i = 0; i < length; i++)
				boot_log[boot_log_length++] = buffer[i];
		} else {
			boot_log_overflow = true;
		}
	}
	spinlock_unlock_irqrestore(&log_lock, flags);
}

void
klog(const char *message)
{
	size_t length = 0;
	while (message[length] != '\0')
		length++;
	log_write(message, length);
}

void
klog_u64(uint64_t value)
{
	char digits[20];
	size_t count = 0;

	do {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0 && count < sizeof(digits));
	while (count != 0) {
		char digit = digits[--count];
		log_write(&digit, 1);
	}
}

void
klog_status(const char *component, bool success, const char *detail)
{
	char line[192];
	size_t length = 0;
	size_t component_length = 0;
	const char *part;

	#define APPEND_STATUS(text) do { \
		part = (text); \
		while (*part != '\0' && length < sizeof(line)) \
			line[length++] = *part++; \
	} while (0)
	APPEND_STATUS("\033[34m*\033[0m Initializing ");
	APPEND_STATUS(component);
	while (component[component_length] != '\0')
		component_length++;
	APPEND_STATUS(" ...");
	while (component_length + 20 < 46) {
		APPEND_STATUS(" ");
		component_length++;
	}
	APPEND_STATUS(" ");
	APPEND_STATUS(success ? "\033[32m[ OK ]\033[0m" :
	    "\033[31m[ ER ]\033[0m");
	if (!success && detail != NULL && detail[0] != '\0') {
		APPEND_STATUS(" ");
		APPEND_STATUS(detail);
	}
	APPEND_STATUS("\n");
	#undef APPEND_STATUS
	log_write(line, length);
}

void
klog_flush_boot_log(void)
{
	struct fd_object *file = NULL;
	uint64_t flags = spinlock_lock_irqsave(&log_lock);
	size_t length = boot_log_length;
	size_t offset = 0;
	bool overflow = boot_log_overflow;
	int error;

	/* Freeze the buffer before filesystem I/O; later logs stay on serial. */
	boot_log_flushed = true;
	spinlock_unlock_irqrestore(&log_lock, flags);
	error = vfs_open("/tmp/btlog.log",
	    VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, 0644, &file);
	if (error != 0) {
		klog("OpenSillicon: could not create /tmp/btlog.log\n");
		return;
	}
	while (offset < length) {
		size_t written = 0;
		error = fd_object_write(file, boot_log + offset,
		    length - offset, &written);
		if (error != 0 || written == 0) {
			klog("OpenSillicon: writing /tmp/btlog.log failed\n");
			break;
		}
		offset += written;
	}
	fd_object_put(file);
	if (overflow)
		klog("OpenSillicon: boot log buffer full; /tmp/btlog.log is truncated\n");
}

_Noreturn void
kernel_halt(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}
