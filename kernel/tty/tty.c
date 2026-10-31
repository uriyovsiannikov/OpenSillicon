#include <stdint.h>

#include "arch/x86_64/ps2_keyboard.h"
#include "kernel/framebuffer.h"
#include "kernel/terminal.h"
#include "kernel/tty.h"

#define TTY_EAGAIN 11
#define TTY_ENODEV 19

static bool renderer_available;

bool
tty_init(const struct limine_framebuffer *framebuffer)
{
	unsigned int columns = 80;
	unsigned int rows = 25;
	terminal_draw_cell draw = NULL;

	renderer_available = framebuffer_text_init(framebuffer, &columns, &rows);
	if (renderer_available)
		draw = framebuffer_text_draw;
	if (!terminal_init(columns, rows, draw, NULL))
		return false;
	(void)ps2_keyboard_init();
	return true;
}

bool
tty_framebuffer_available(void)
{
	return renderer_available;
}

bool
tty_keyboard_available(void)
{
	return ps2_keyboard_available();
}

int
tty_read(void *buffer, size_t length, size_t *read_length)
{
	struct terminal_key_event event;
	int error;

	if (read_length == NULL || (buffer == NULL && length != 0))
		return -22;
	*read_length = 0;
	if (length == 0)
		return 0;
	for (;;) {
		error = terminal_read(buffer, length, read_length);
		if (error == 0)
			return 0;
		if (error != -TTY_EAGAIN)
			return error;
		if (!ps2_keyboard_available())
			return -TTY_ENODEV;
		if (ps2_keyboard_poll(&event))
			terminal_key_event(event);
		else
			__asm__ volatile ("sti; hlt; cli" : : : "memory");
	}
}

bool
tty_write(const void *buffer, size_t length)
{
	if (buffer == NULL && length != 0)
		return false;
	terminal_write(buffer, length);
	return true;
}

_Noreturn void
tty_console_loop(void)
{
	for (;;) {
		struct terminal_key_event event;

		if (ps2_keyboard_available() && ps2_keyboard_poll(&event))
			terminal_key_event(event);
		else
			__asm__ volatile ("sti; hlt; cli" : : : "memory");
	}
}
