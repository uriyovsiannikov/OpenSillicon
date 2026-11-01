#include <stdint.h>
#include <stddef.h>

#include "kernel/log.h"
#include "kernel/tty.h"

#define COM1 0x3f8
#define BOOT_STATUS_BUFFER_SIZE 4096U

static char pending_boot_status[BOOT_STATUS_BUFFER_SIZE];
static size_t pending_boot_status_length;

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

void
klog(const char *message)
{
	while (*message != '\0') {
		if (*message == '\n')
			serial_putc('\r');
		serial_putc(*message++);
	}
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
	while (count != 0)
		serial_putc(digits[--count]);
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
	(void)serial_write(line, length);
	if (tty_framebuffer_available()) {
		if (pending_boot_status_length != 0) {
			(void)tty_write(pending_boot_status,
			    pending_boot_status_length);
			pending_boot_status_length = 0;
		}
		(void)tty_write(line, length);
	} else {
		/* Keep complete status lines only. In particular, never truncate an
		 * ANSI escape sequence and leave the terminal parser in CSI state. */
		if (length <= sizeof(pending_boot_status) -
		    pending_boot_status_length) {
			size_t i;
			for (i = 0; i < length; i++)
				pending_boot_status[pending_boot_status_length++] =
				    line[i];
		}
	}
}

_Noreturn void
kernel_halt(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}
