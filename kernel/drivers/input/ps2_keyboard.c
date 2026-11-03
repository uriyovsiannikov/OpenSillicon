#include "arch/x86_64/ps2_keyboard.h"
#include "abi/input.h"
#include "kernel/timer.h"

#define PS2_DATA_PORT 0x60U
#define PS2_STATUS_PORT 0x64U
#define PS2_COMMAND_PORT 0x64U
#define PS2_STATUS_OUTPUT_FULL 0x01U
#define PS2_STATUS_INPUT_FULL 0x02U
#define PS2_STATUS_AUXILIARY 0x20U
#define PS2_TIMEOUT_TICKS 100U

static struct ps2_keyboard_decoder decoder;
static bool initialized;
static enum ps2_keyboard_error initialization_error;
#define KEYBOARD_EVENT_QUEUE_CAPACITY 128U
static struct terminal_key_event event_queue[KEYBOARD_EVENT_QUEUE_CAPACITY];
static size_t event_queue_head;
static size_t event_queue_count;
static bool event_capture_enabled;
#define MOUSE_EVENT_QUEUE_CAPACITY 64U
#define PS2_POLL_DRAIN_LIMIT 128U
static struct os_mouse_event mouse_event_queue[MOUSE_EVENT_QUEUE_CAPACITY];
static size_t mouse_event_head;
static size_t mouse_event_count;
static bool mouse_initialized;
static bool mouse_capture_enabled;
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_count;

static void
queue_mouse_event(struct os_mouse_event event)
{
	size_t tail;

	if (mouse_event_count != 0) {
		struct os_mouse_event *last;
		int32_t dx, dy;

		tail = (mouse_event_head + mouse_event_count - 1) %
		    MOUSE_EVENT_QUEUE_CAPACITY;
		last = &mouse_event_queue[tail];
		if (last->buttons == event.buttons) {
			dx = (int32_t)last->dx + event.dx;
			dy = (int32_t)last->dy + event.dy;
			if (dx > INT16_MAX) dx = INT16_MAX;
			if (dx < INT16_MIN) dx = INT16_MIN;
			if (dy > INT16_MAX) dy = INT16_MAX;
			if (dy < INT16_MIN) dy = INT16_MIN;
			last->dx = (int16_t)dx;
			last->dy = (int16_t)dy;
			return;
		}
	}
	if (mouse_event_count == MOUSE_EVENT_QUEUE_CAPACITY) {
		/* Preserve recent button transitions instead of growing stale input. */
		mouse_event_head = (mouse_event_head + 1) %
		    MOUSE_EVENT_QUEUE_CAPACITY;
		mouse_event_count--;
	}
	tail = (mouse_event_head + mouse_event_count) %
	    MOUSE_EVENT_QUEUE_CAPACITY;
	mouse_event_queue[tail] = event;
	mouse_event_count++;
}

static void
queue_event(struct terminal_key_event event)
{
	size_t tail;

	if (event_queue_count == KEYBOARD_EVENT_QUEUE_CAPACITY)
		return;
	tail = (event_queue_head + event_queue_count) %
	    KEYBOARD_EVENT_QUEUE_CAPACITY;
	event_queue[tail] = event;
	event_queue_count++;
}

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

static bool
mouse_response(uint8_t *response)
{
	uint64_t start = timer_ticks();
	uint64_t flags;
	bool ready = false;

	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	while (timer_ticks() - start < PS2_TIMEOUT_TICKS) {
		uint8_t status = inb(PS2_STATUS_PORT);
		if ((status & PS2_STATUS_OUTPUT_FULL) != 0) {
			uint8_t byte = inb(PS2_DATA_PORT);
			if ((status & PS2_STATUS_AUXILIARY) != 0) {
				*response = byte;
				ready = true;
				break;
			}
			if (initialized) {
				struct terminal_key_event event;
				if (ps2_keyboard_decode_byte(&decoder, byte, &event) &&
				    event_capture_enabled)
					queue_event(event);
			}
		} else {
			__asm__ volatile ("sti; hlt; cli" : : : "memory");
		}
	}
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
	return ready;
}

static bool
mouse_command(uint8_t command)
{
	uint8_t response;

	if (!controller_command(0xd4) || !controller_data(command) ||
	    !mouse_response(&response))
		return false;
	return response == 0xfa;
}

bool
ps2_mouse_init(void)
{
	uint8_t configuration;

	mouse_initialized = false;
	mouse_packet_count = 0;
	mouse_event_head = 0;
	mouse_event_count = 0;
	mouse_capture_enabled = false;
	if (!initialized || !controller_command(0xa8) ||
	    !controller_command(0x20) || !read_data(&configuration))
		return false;
	configuration &= (uint8_t)~0x22U;
	if (!controller_command(0x60) || !controller_data(configuration) ||
	    !mouse_command(0xf6) || !mouse_command(0xf4)) {
		(void)controller_command(0xa7);
		return false;
	}
	mouse_initialized = true;
	return true;
}

bool
ps2_mouse_available(void)
{
	return mouse_initialized;
}

static void
ps2_mouse_decode_byte(uint8_t byte)
{
	struct os_mouse_event event;
	uint8_t first;
	int dx, dy;

	if (mouse_packet_count == 0 && (byte & 0x08U) == 0)
		return;
	mouse_packet[mouse_packet_count++] = byte;
	if (mouse_packet_count < sizeof(mouse_packet))
		return;
	mouse_packet_count = 0;
	first = mouse_packet[0];
	dx = (int8_t)mouse_packet[1];
	dy = -(int8_t)mouse_packet[2];
	if ((first & 0x40U) != 0)
		dx = 0;
	if ((first & 0x80U) != 0)
		dy = 0;
	event.dx = (int16_t)dx;
	event.dy = (int16_t)dy;
	event.buttons = first & 0x07U;
	event.reserved[0] = 0;
	event.reserved[1] = 0;
	event.reserved[2] = 0;
	if (mouse_capture_enabled)
		queue_mouse_event(event);
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
	event_queue_head = 0;
	event_queue_count = 0;
	event_capture_enabled = false;
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

static bool
poll_keyboard_event(struct terminal_key_event *event)
{
	uint8_t status;
	uint8_t byte;

	if (!initialized || event == NULL)
		return false;
	status = inb(PS2_STATUS_PORT);
	if ((status & PS2_STATUS_OUTPUT_FULL) == 0)
		return false;
	byte = inb(PS2_DATA_PORT);
	if ((status & PS2_STATUS_AUXILIARY) != 0) {
		ps2_mouse_decode_byte(byte);
		return false;
	}
	if (!ps2_keyboard_decode_byte(&decoder, byte, event))
		return false;
	if (event_capture_enabled)
		queue_event(*event);
	return true;
}

bool
ps2_keyboard_poll(struct terminal_key_event *event)
{
	/* Returning to terminal input ends raw event capture. Otherwise shell
	 * keystrokes survive in the graphics queue until the next X server. */
	event_capture_enabled = false;
	event_queue_head = 0;
	event_queue_count = 0;
	return poll_keyboard_event(event);
}

bool
ps2_mouse_event_poll(struct os_mouse_event *event)
{
	uint8_t status;
	uint8_t byte;
	struct terminal_key_event key_event;
	unsigned int drained = 0;

	if (event == NULL || !mouse_initialized)
		return false;
	mouse_capture_enabled = true;
	while (drained < PS2_POLL_DRAIN_LIMIT) {
		status = inb(PS2_STATUS_PORT);
		if ((status & PS2_STATUS_OUTPUT_FULL) == 0)
			break;
		byte = inb(PS2_DATA_PORT);
		drained++;
		if ((status & PS2_STATUS_AUXILIARY) != 0)
			ps2_mouse_decode_byte(byte);
		else if (ps2_keyboard_decode_byte(&decoder, byte, &key_event) &&
		    event_capture_enabled)
			queue_event(key_event);
	}
	if (mouse_event_count == 0)
		return false;
	*event = mouse_event_queue[mouse_event_head];
	mouse_event_head = (mouse_event_head + 1) %
	    MOUSE_EVENT_QUEUE_CAPACITY;
	mouse_event_count--;
	return true;
}

bool
ps2_keyboard_event_poll(struct terminal_key_event *event)
{
	struct terminal_key_event discarded;

	if (event == NULL || !initialized)
		return false;
	event_capture_enabled = true;
	(void)poll_keyboard_event(&discarded);
	if (event_queue_count == 0)
		return false;
	*event = event_queue[event_queue_head];
	event_queue_head = (event_queue_head + 1) % KEYBOARD_EVENT_QUEUE_CAPACITY;
	event_queue_count--;
	return true;
}
