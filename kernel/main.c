#include <stdint.h>
#include "limine.h"
#include "kernel/boot.h"

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_firmware_type_request firmware_request = {
	.id = LIMINE_FIRMWARE_TYPE_REQUEST_ID
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST_ID
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST_ID
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
	.id = LIMINE_FRAMEBUFFER_REQUEST_ID
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

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

static void
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

static void
klog(const char *message)
{
	while (*message != '\0') {
		if (*message == '\n')
			serial_putc('\r');
		serial_putc(*message++);
	}
}

static void
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

static void
halt(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

void
_start(void)
{
	struct boot_info info;
	struct bootstrap_allocator allocator;
	void *allocation;
	enum boot_info_error error;

	serial_init();
	error = boot_info_parse(firmware_request.response, hhdm_request.response,
	    memmap_request.response, framebuffer_request.response, &info);
	if (error != BOOT_INFO_OK) {
		klog("OpenSillicon: invalid boot information, error ");
		klog_u64((uint64_t)error);
		klog("\n");
		halt();
	}
	if (!bootstrap_allocator_init(&allocator, &info)) {
		klog("OpenSillicon: bootstrap allocator initialization failed\n");
		halt();
	}
	allocation = bootstrap_alloc(&allocator, 16, 16);
	if (allocation == NULL) {
		klog("OpenSillicon: bootstrap allocation failed\n");
		halt();
	}
	klog("OpenSillicon: UEFI boot information parsed\n");
	klog("OpenSillicon: memory map entries ");
	klog_u64(info.memmap->entry_count);
	klog("\nOpenSillicon: HHDM offset ");
	klog_u64(info.hhdm_offset);
	klog("\n");
	if (info.framebuffer != NULL) {
		klog("OpenSillicon: framebuffer ");
		klog_u64(info.framebuffer->width);
		serial_putc('x');
		klog_u64(info.framebuffer->height);
		klog("\n");
	} else {
		klog("OpenSillicon: no framebuffer available\n");
	}
	klog("OpenSillicon: bootstrap allocator ready\n");
	halt();
}
