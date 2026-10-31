#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "arch/x86_64/ps2_keyboard.h"
#include "kernel/framebuffer.h"
#include "kernel/terminal.h"
#include "kernel/tty.h"

static unsigned int drawn_terminal;
static unsigned int draw_count;

static void
record_draw(void *context, unsigned int terminal, unsigned int column,
	unsigned int row, struct terminal_cell cell)
{
	(void)context;
	(void)column;
	(void)row;
	(void)cell;
	drawn_terminal = terminal;
	draw_count++;
}

static void
key(uint8_t scancode, bool pressed)
{
	terminal_key_event((struct terminal_key_event){
		.scancode = scancode,
		.pressed = pressed
	});
}

static void
test_virtual_terminals(void)
{
	char input[TERMINAL_LINE_CAPACITY];
	size_t length;

	assert(terminal_init(8, 3, record_draw, NULL));
	terminal_write("A", 1);
	assert(terminal_cell_at(0, 0, 0).character == 'A');
	assert(terminal_switch(1));
	terminal_write("B", 1);
	assert(terminal_cell_at(1, 0, 0).character == 'B');
	assert(terminal_cell_at(0, 0, 0).character == 'A');
	key(0x30, true);
	assert(terminal_switch(0));
	key(0x1e, true);
	key(0x1c, true);
	assert(terminal_read(input, sizeof(input), &length) == 0);
	assert(length == 2 && input[0] == 'a' && input[1] == '\n');
	assert(terminal_switch(1));
	key(0x1c, true);
	assert(terminal_read(input, sizeof(input), &length) == 0);
	assert(length == 2 && input[0] == 'b' && input[1] == '\n');
	assert(draw_count != 0 && drawn_terminal == 1);
	assert(terminal_switch(0));
	key(0x38, true);
	key(0x3c, true);
	assert(terminal_active() == 1);
	key(0x3c, false);
	key(0x38, false);
}

static void
test_tty_limits_and_eof(void)
{
	char input[TERMINAL_LINE_CAPACITY];
	size_t length;
	unsigned int i;

	assert(terminal_init(80, 25, NULL, NULL));
	for (i = 0; i < TERMINAL_LINE_CAPACITY + 20; i++)
		key(0x1e, true);
	key(0x1c, true);
	assert(terminal_read(input, sizeof(input), &length) == 0);
	assert(length == TERMINAL_LINE_CAPACITY);
	for (i = 0; i < TERMINAL_LINE_CAPACITY - 1; i++)
		assert(input[i] == 'a');
	assert(input[TERMINAL_LINE_CAPACITY - 1] == '\n');

	key(0x1d, true);
	key(0x20, true);
	key(0x20, false);
	key(0x1d, false);
	assert(terminal_read(input, sizeof(input), &length) == 0);
	assert(length == 0);
	assert(terminal_read(input, sizeof(input), &length) == -11);

	key(0x1e, true);
	key(0x1d, true);
	key(0x20, true);
	key(0x20, false);
	key(0x1d, false);
	assert(terminal_read(input, sizeof(input), &length) == 0);
	assert(length == 1 && input[0] == 'a');
	assert(terminal_read(input, sizeof(input), &length) == -11);
}

static void
test_scancode_decoder(void)
{
	struct ps2_keyboard_decoder decoder;
	struct terminal_key_event event;

	ps2_keyboard_decoder_init(&decoder);
	assert(!ps2_keyboard_decode_byte(&decoder, 0xe0, &event));
	assert(ps2_keyboard_decode_byte(&decoder, 0x1d, &event));
	assert(event.extended && event.pressed && event.scancode == 0x1d);
	assert(!ps2_keyboard_decode_byte(&decoder, 0xe1, &event));
	assert(!ps2_keyboard_decode_byte(&decoder, 0x1d, &event));
	assert(!ps2_keyboard_decode_byte(&decoder, 0x45, &event));
	assert(!ps2_keyboard_decode_byte(&decoder, 0xe1, &event));
	assert(!ps2_keyboard_decode_byte(&decoder, 0x9d, &event));
	assert(!ps2_keyboard_decode_byte(&decoder, 0xc5, &event));
	assert(ps2_keyboard_decode_byte(&decoder, 0x9e, &event));
	assert(!event.pressed && !event.extended && event.scancode == 0x1e);
}

static void
test_terminal_controls_and_absent_keyboard(void)
{
	char input[TERMINAL_LINE_CAPACITY];
	size_t length;

	assert(terminal_init(4, 2, NULL, NULL));
	terminal_write("one\nabcd\ne", 10);
	assert(terminal_cell_at(0, 0, 0).character == 'a');
	assert(terminal_cell_at(0, 0, 1).character == 'e');
	terminal_write("\033[2J\033[31;44mX", 13);
	assert(terminal_cell_at(0, 0, 0).character == 'X');
	assert(terminal_cell_at(0, 0, 0).foreground == 1);
	assert(terminal_cell_at(0, 0, 0).background == 4);
	terminal_write("\033[H\033[K", 7);
	assert(terminal_cell_at(0, 0, 0).character == ' ');

	assert(terminal_init(80, 25, NULL, NULL));
	key(0x1e, true);
	key(0x30, true);
	key(0x0e, true);
	key(0x1c, true);
	assert(terminal_read(input, sizeof(input), &length) == 0);
	assert(length == 2 && input[0] == 'a' && input[1] == '\n');
	assert(tty_read(input, sizeof(input), &length) == -19);
}

static void
test_sgr_split_reset_and_malformed(void)
{
	struct terminal_cell cell;

	assert(terminal_init(16, 1, NULL, NULL));
	terminal_write("\033[3", 3);
	terminal_write("2mG", 3);
	cell = terminal_cell_at(0, 0, 0);
	assert(cell.character == 'G' && cell.foreground == 2);
	terminal_write("\033[0mR", 5);
	cell = terminal_cell_at(0, 1, 0);
	assert(cell.character == 'R' && cell.foreground == 7 &&
	    cell.background == 0);
	{
		static const char malformed[] = "\033[999999999999999999999999mM";
		terminal_write(malformed, sizeof(malformed) - 1);
	}
	cell = terminal_cell_at(0, 2, 0);
	assert(cell.character == 'M' && cell.foreground == 7);
	terminal_write("\033[31?X", 6);
	cell = terminal_cell_at(0, 3, 0);
	assert(cell.character == 'X' && cell.foreground == 7);
}

static void
test_framebuffer_renderer(void)
{
	uint8_t pixels[16 * 16 * 4];
	struct limine_framebuffer framebuffer = {
		.address = pixels,
		.width = 16,
		.height = 16,
		.pitch = 16 * 4,
		.bpp = 32,
		.memory_model = LIMINE_FRAMEBUFFER_RGB,
		.red_mask_size = 8,
		.red_mask_shift = 16,
		.green_mask_size = 8,
		.green_mask_shift = 8,
		.blue_mask_size = 8,
		.blue_mask_shift = 0
	};
	unsigned int columns;
	unsigned int rows;

	memset(pixels, 0xff, sizeof(pixels));
	assert(framebuffer_text_init(&framebuffer, &columns, &rows));
	assert(columns == 2 && rows == 1);
	assert(terminal_init(columns, rows, framebuffer_text_draw, NULL));
	terminal_write("\033[91;104mA", 10);
	assert(pixels[0] == 0xff && pixels[1] == 0x55 && pixels[2] == 0x55);
	assert(pixels[2 * 16 * 4 + 2 * 4] == 0x55);
	assert(pixels[2 * 16 * 4 + 2 * 4 + 1] == 0x55);
	assert(pixels[2 * 16 * 4 + 2 * 4 + 2] == 0xff);
	assert(!framebuffer_text_init(NULL, &columns, &rows));
}

int
main(void)
{
	test_virtual_terminals();
	test_tty_limits_and_eof();
	test_terminal_controls_and_absent_keyboard();
	test_sgr_split_reset_and_malformed();
	test_scancode_decoder();
	test_framebuffer_renderer();
	puts("terminal rendering, VT isolation, keyboard, and TTY tests passed");
	return 0;
}
