#include <stdint.h>
#include <stddef.h>

#include "kernel/log.h"

#define COM1 0x3f8

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

_Noreturn void
kernel_halt(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}
