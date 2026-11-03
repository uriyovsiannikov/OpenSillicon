#include <stdint.h>

#include "arch/x86_64/ps2_keyboard.h"
#include "kernel/framebuffer.h"
#include "kernel/terminal.h"
#include "kernel/timer.h"
#include "kernel/tty.h"

#define TTY_EAGAIN 11
#define TTY_ENODEV 19
#define TTY_RAW_READ_TIMEOUT_TICKS 50U

static bool renderer_available;
static bool framebuffer_output_enabled = true;
static uint64_t framebuffer_output_owner;
static unsigned int tty_columns;
static unsigned int tty_rows;

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
	tty_columns = columns;
	tty_rows = rows;
	(void)ps2_keyboard_init();
	(void)ps2_mouse_init();
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

bool
tty_dimensions(unsigned short *rows, unsigned short *columns)
{
	if (rows == NULL || columns == NULL || tty_columns == 0 || tty_rows == 0 ||
	    tty_columns > UINT16_MAX || tty_rows > UINT16_MAX)
		return false;
	*rows = (unsigned short)tty_rows;
	*columns = (unsigned short)tty_columns;
	return true;
}

int
tty_read(void *buffer, size_t length, size_t *read_length)
{
	struct terminal_key_event event;
	uint64_t wait_start = timer_ticks();
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
		else {
			__asm__ volatile ("sti; hlt; cli" : : : "memory");
			if (terminal_raw_mode() && timer_ticks() - wait_start >=
			    TTY_RAW_READ_TIMEOUT_TICKS)
				return -TTY_EAGAIN;
		}
	}
}

bool
tty_write(const void *buffer, size_t length)
{
	if (buffer == NULL && length != 0)
		return false;
	if (!framebuffer_output_enabled)
		return true;
	terminal_write(buffer, length);
	return true;
}

bool
tty_console_framebuffer_set(bool enabled, uint64_t owner_pid)
{
	if (owner_pid == 0)
		return false;
	if (!enabled) {
		if (framebuffer_output_owner != 0 &&
		    framebuffer_output_owner != owner_pid)
			return false;
		framebuffer_output_enabled = false;
		framebuffer_output_owner = owner_pid;
		return true;
	}
	if (framebuffer_output_owner != owner_pid)
		return false;
	framebuffer_output_enabled = true;
	framebuffer_output_owner = 0;
	(void)terminal_switch(terminal_active());
	return true;
}

void
tty_console_framebuffer_owner_exit(uint64_t owner_pid)
{
	if (owner_pid != 0 && framebuffer_output_owner == owner_pid) {
		framebuffer_output_enabled = true;
		framebuffer_output_owner = 0;
		(void)terminal_switch(terminal_active());
	}
}

bool
tty_set_raw_mode(bool enabled)
{
	if (!ps2_keyboard_available())
		return false;
	terminal_set_raw_mode(enabled);
	return true;
}

bool
tty_raw_mode(void)
{
	return terminal_raw_mode();
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
