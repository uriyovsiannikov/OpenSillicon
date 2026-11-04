#include <stdint.h>
#include <stddef.h>

#include "kernel/log.h"
#include "kernel/fd.h"
#include "kernel/framebuffer.h"
#include "kernel/spinlock.h"
#include "kernel/vfs.h"

#define COM1 0x3f8
#define BOOT_LOG_BUFFER_SIZE (64U * 1024U)
#define PANIC_CONTEXT_SIZE 1024U

static char boot_log[BOOT_LOG_BUFFER_SIZE];
static size_t boot_log_length;
static bool boot_log_overflow;
static bool boot_log_flushed;
static struct spinlock log_lock = SPINLOCK_INITIALIZER;
static volatile char panic_context[PANIC_CONTEXT_SIZE];
static volatile size_t panic_context_next;
static volatile size_t panic_context_length;
static volatile uint32_t panic_started;

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

	/* Keep boot messages off COM1 while retaining their panic context. */
	for (i = 0; i < length; i++) {
		panic_context[panic_context_next] = buffer[i];
		panic_context_next = (panic_context_next + 1U) %
		    PANIC_CONTEXT_SIZE;
		if (panic_context_length < PANIC_CONTEXT_SIZE)
			panic_context_length++;
	}
	if (boot_log_flushed) {
		for (i = 0; i < length; i++) {
			if (buffer[i] == '\n')
				serial_putc('\r');
			serial_putc(buffer[i]);
		}
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
	char output[20];
	size_t count = 0;

	do {
		digits[count++] = (char)('0' + value % 10);
		value /= 10;
	} while (value != 0 && count < sizeof(digits));
	for (size_t i = 0; i < count; i++)
		output[i] = digits[count - i - 1U];
	log_write(output, count);
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
	/* Boot messages are preserved in /tmp/btlog.log, but must not be replayed
	 * to COM1 or the panic screen if a later runtime panic occurs. */
	panic_context_next = 0;
	panic_context_length = 0;
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
	static const char serial_header[] =
	    "\n*** OpenSillicon KERNEL PANIC / HALT ***\nRecent kernel diagnostics:\n";
	char diagnostic[PANIC_CONTEXT_SIZE + 1U];
	size_t length, begin;
	__asm__ volatile ("cli" : : : "memory");
	if (__atomic_exchange_n(&panic_started, 1U, __ATOMIC_ACQ_REL) != 0) {
		for (;;)
			__asm__ volatile ("hlt");
	}
	length = panic_context_length;
	begin = (panic_context_next + PANIC_CONTEXT_SIZE - length) %
	    PANIC_CONTEXT_SIZE;
	for (size_t i = 0; i < length; i++)
		diagnostic[i] = panic_context[(begin + i) % PANIC_CONTEXT_SIZE];
	diagnostic[length] = '\0';
	(void)serial_write(serial_header, sizeof(serial_header) - 1U);
	if (length != 0)
		(void)serial_write(diagnostic, length);
	if (length == 0 || diagnostic[length - 1U] != '\n')
		(void)serial_write("\n", 1);
	framebuffer_panic_screen(diagnostic);
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}
