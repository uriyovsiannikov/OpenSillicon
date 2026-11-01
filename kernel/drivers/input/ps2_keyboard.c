#include "arch/x86_64/ps2_keyboard.h"
#include "kernel/timer.h"

#define PS2_DATA_PORT 0x60U
#define PS2_STATUS_PORT 0x64U
#define PS2_COMMAND_PORT 0x64U
#define PS2_STATUS_OUTPUT_FULL 0x01U
#define PS2_STATUS_INPUT_FULL 0x02U
#define PS2_TIMEOUT_TICKS 100U

static struct ps2_keyboard_decoder decoder;
static bool initialized;
static enum ps2_keyboard_error initialization_error;

static inline uint8_t
inb(uint16_t port)
{
	uint8_t value;

	__asm__ volatile ("inb %1, %0" : "=a" (value) : "Nd" (port));
	return value;
}

static inline void
outb(uint16_t port, uint8_t value)
{
	__asm__ volatile ("outb %0, %1" : : "a" (value), "Nd" (port));
}

static bool
wait_status(uint8_t mask, bool set)
{
	uint64_t flags;
	uint64_t start = timer_ticks();
	bool ready;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	for (;;) {
		ready = ((inb(PS2_STATUS_PORT) & mask) != 0) == set;
		if (ready || timer_ticks() - start >= PS2_TIMEOUT_TICKS)
			break;
		__asm__ volatile ("sti; hlt; cli" : : : "memory");
	}
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return ready;
}

static bool
controller_command(uint8_t command)
{
	if (!wait_status(PS2_STATUS_INPUT_FULL, false))
		return false;
	outb(PS2_COMMAND_PORT, command);
	return true;
}

static bool
controller_data(uint8_t value)
{
	if (!wait_status(PS2_STATUS_INPUT_FULL, false))
		return false;
	outb(PS2_DATA_PORT, value);
	return true;
}

static bool
read_data(uint8_t *value)
{
	if (!wait_status(PS2_STATUS_OUTPUT_FULL, true))
		return false;
	*value = inb(PS2_DATA_PORT);
	return true;
}

static bool
keyboard_command(uint8_t command)
{
	uint8_t response;

	if (!controller_data(command) || !read_data(&response))
		return false;
	return response == 0xfa;
}

bool
ps2_keyboard_init(void)
{
	uint8_t configuration;
	unsigned int i;

	initialized = false;
	initialization_error = PS2_KEYBOARD_OK;
	if (!controller_command(0xad)) {
		initialization_error = PS2_KEYBOARD_CONTROLLER_TIMEOUT;
		return false;
	}
	for (i = 0; i < 32; i++) {
		if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) == 0)
			break;
		(void)inb(PS2_DATA_PORT);
	}
	if (!controller_command(0x20) || !read_data(&configuration)) {
		initialization_error = PS2_KEYBOARD_CONFIG_READ_TIMEOUT;
		return false;
	}
	configuration &= (uint8_t)~0x03U;
	configuration &= (uint8_t)~0x10U;
	configuration |= 0x40U;
	if (!controller_command(0x60) || !controller_data(configuration)) {
		initialization_error = PS2_KEYBOARD_CONFIG_WRITE_TIMEOUT;
		return false;
	}
	if (!controller_command(0xae)) {
		initialization_error = PS2_KEYBOARD_PORT_ENABLE_FAILED;
		return false;
	}
	if (!keyboard_command(0xf4)) {
		initialization_error = PS2_KEYBOARD_SCAN_ENABLE_FAILED;
		return false;
	}
	ps2_keyboard_decoder_init(&decoder);
	initialized = true;
	return true;
}

bool
ps2_keyboard_available(void)
{
	return initialized;
}

enum ps2_keyboard_error
ps2_keyboard_last_error(void)
{
	return initialization_error;
}

bool
ps2_keyboard_poll(struct terminal_key_event *event)
{
	uint8_t status;
	uint8_t byte;

	if (!initialized || event == NULL)
		return false;
	status = inb(PS2_STATUS_PORT);
	if ((status & PS2_STATUS_OUTPUT_FULL) == 0)
		return false;
	byte = inb(PS2_DATA_PORT);
	return ps2_keyboard_decode_byte(&decoder, byte, event);
}
