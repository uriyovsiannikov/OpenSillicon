#include <stddef.h>
#include <stdint.h>

#include "kernel/framebuffer.h"
#include "kernel/terminal.h"

#include "font.h"

#define FONT_WIDTH 12U
#define FONT_HEIGHT 22U

static const struct limine_framebuffer *active_framebuffer;
static unsigned int framebuffer_columns;
static unsigned int framebuffer_rows;
static const uint32_t palette[16] = {
	0x000000, 0xaa0000, 0x00aa00, 0xaa5500,
	0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa,
	0x555555, 0xff5555, 0x55ff55, 0xffff55,
	0x5555ff, 0xff55ff, 0x55ffff, 0xffffff
};

static uint32_t
scale_channel(uint8_t channel, uint8_t size, uint8_t shift)
{
	uint64_t maximum;
	uint64_t scaled;

	if (size == 0 || size > 8)
		return 0;
	maximum = (1ULL << size) - 1;
	scaled = ((uint64_t)channel * maximum + 127) / 255;
	return (uint32_t)(scaled << shift);
}

static uint32_t
pixel_value(uint32_t color)
{
	uint8_t red = (uint8_t)(color >> 16);
	uint8_t green = (uint8_t)(color >> 8);
	uint8_t blue = (uint8_t)color;

	return scale_channel(red, active_framebuffer->red_mask_size,
	    active_framebuffer->red_mask_shift) |
	    scale_channel(green, active_framebuffer->green_mask_size,
	    active_framebuffer->green_mask_shift) |
	    scale_channel(blue, active_framebuffer->blue_mask_size,
	    active_framebuffer->blue_mask_shift);
}

static void
put_pixel(uint64_t x, uint64_t y, uint32_t color)
{
	uint8_t *address = active_framebuffer->address;
	uint64_t bytes_per_pixel = (active_framebuffer->bpp + 7U) / 8U;
	uint64_t offset = y * active_framebuffer->pitch + x * bytes_per_pixel;
	uint32_t value = pixel_value(color);
	uint64_t byte;

	for (byte = 0; byte < bytes_per_pixel; byte++)
		address[offset + byte] = (uint8_t)(value >> (byte * 8));
}

static void
fill(uint32_t color)
{
	uint64_t x;
	uint64_t y;

	for (y = 0; y < active_framebuffer->height; y++) {
		for (x = 0; x < active_framebuffer->width; x++)
			put_pixel(x, y, color);
	}
}

bool
framebuffer_text_init(const struct limine_framebuffer *framebuffer,
	unsigned int *columns, unsigned int *rows)
{
	uint64_t bytes_per_pixel;
	uint64_t minimum_pitch;
	uint64_t max_columns;
	uint64_t max_rows;
	uint64_t framebuffer_size;
	uint64_t red_mask;
	uint64_t green_mask;
	uint64_t blue_mask;

	active_framebuffer = NULL;
	framebuffer_columns = 0;
	framebuffer_rows = 0;
	if (framebuffer == NULL || framebuffer->address == NULL ||
	    framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
	    framebuffer->bpp < 15 || framebuffer->bpp > 32 ||
	    framebuffer->red_mask_size == 0 || framebuffer->green_mask_size == 0 ||
	    framebuffer->blue_mask_size == 0 ||
	    framebuffer->red_mask_size > 8 || framebuffer->green_mask_size > 8 ||
	    framebuffer->blue_mask_size > 8 ||
	    framebuffer->red_mask_shift + framebuffer->red_mask_size >
	    framebuffer->bpp || framebuffer->green_mask_shift +
	    framebuffer->green_mask_size > framebuffer->bpp ||
	    framebuffer->blue_mask_shift + framebuffer->blue_mask_size >
	    framebuffer->bpp || columns == NULL || rows == NULL)
		return false;
	bytes_per_pixel = (framebuffer->bpp + 7U) / 8U;
	if (framebuffer->width > UINT64_MAX / bytes_per_pixel)
		return false;
	minimum_pitch = framebuffer->width * bytes_per_pixel;
	if (framebuffer->pitch < minimum_pitch || framebuffer->height == 0 ||
	    framebuffer->height > UINT64_MAX / framebuffer->pitch ||
	    framebuffer->width < FONT_WIDTH || framebuffer->height < FONT_HEIGHT)
		return false;
	framebuffer_size = framebuffer->pitch * framebuffer->height;
	if (framebuffer_size == 0 || framebuffer_size - 1 > UINTPTR_MAX -
	    (uintptr_t)framebuffer->address)
		return false;
	red_mask = ((1ULL << framebuffer->red_mask_size) - 1) <<
	    framebuffer->red_mask_shift;
	green_mask = ((1ULL << framebuffer->green_mask_size) - 1) <<
	    framebuffer->green_mask_shift;
	blue_mask = ((1ULL << framebuffer->blue_mask_size) - 1) <<
	    framebuffer->blue_mask_shift;
	if ((red_mask & green_mask) != 0 || (red_mask & blue_mask) != 0 ||
	    (green_mask & blue_mask) != 0)
		return false;
	active_framebuffer = framebuffer;
	max_columns = framebuffer->width / FONT_WIDTH;
	max_rows = framebuffer->height / FONT_HEIGHT;
	if (max_columns > TERMINAL_MAX_COLUMNS)
		max_columns = TERMINAL_MAX_COLUMNS;
	if (max_rows > TERMINAL_MAX_ROWS)
		max_rows = TERMINAL_MAX_ROWS;
	*columns = (unsigned int)max_columns;
	*rows = (unsigned int)max_rows;
	framebuffer_columns = *columns;
	framebuffer_rows = *rows;
	fill(0);
	return true;
}

void
framebuffer_text_draw(void *context, unsigned int terminal,
	unsigned int column, unsigned int row, struct terminal_cell cell)
{
	uint32_t foreground;
	uint32_t background;
	uint64_t origin_x;
	uint64_t origin_y;
	unsigned int glyph_row;
	unsigned int x;
	unsigned int glyph;

	(void)context;
	if (active_framebuffer == NULL || terminal != terminal_active() ||
	    column >= framebuffer_columns || row >= framebuffer_rows)
		return;
	foreground = palette[cell.foreground & 15U];
	background = palette[cell.background & 15U];
	glyph = cell.character < 0x80U ? cell.character :
	    cell.character >= 0xa0U ? 128U + cell.character - 0xa0U : 224U;
	origin_x = (uint64_t)column * FONT_WIDTH;
	origin_y = (uint64_t)row * FONT_HEIGHT;
	for (glyph_row = 0; glyph_row < FONT_HEIGHT; glyph_row++) {
		uint16_t bits = gallant12x22[glyph][glyph_row];

		for (x = 0; x < FONT_WIDTH; x++) {
			uint32_t color = (bits & (0x800U >> x)) != 0 ? foreground :
			    background;

			put_pixel(origin_x + x, origin_y + glyph_row, color);
		}
	}
}

static void
panic_draw_char(unsigned int column, unsigned int row, uint8_t character)
{
	uint64_t origin_x = (uint64_t)column * FONT_WIDTH;
	uint64_t origin_y = (uint64_t)row * FONT_HEIGHT;
	unsigned int glyph = character < 0x80U ? character : 224U;

	for (unsigned int y = 0; y < FONT_HEIGHT; y++) {
		uint16_t bits = gallant12x22[glyph][y];
		for (unsigned int x = 0; x < FONT_WIDTH; x++)
			put_pixel(origin_x + x, origin_y + y,
			    (bits & (0x800U >> x)) != 0 ? 0xffffffU : 0x880000U);
	}
}

static void
panic_draw_text(unsigned int *column, unsigned int *row, const char *text)
{
	while (*text != '\0' && *row < framebuffer_rows) {
		if ((uint8_t)*text == 0x1bU) {
			text++;
			if (*text == '[') {
				text++;
				while (*text != '\0' && !(*text >= '@' && *text <= '~'))
					text++;
				if (*text != '\0')
					text++;
			}
			continue;
		}
		if (*text == '\n' || *column >= framebuffer_columns - 1U) {
			*column = 2;
			(*row)++;
			if (*text == '\n') {
				text++;
				continue;
			}
		}
		if (*row >= framebuffer_rows)
			break;
		panic_draw_char((*column)++, (*row), (uint8_t)*text++);
	}
}

void
framebuffer_panic_screen(const char *diagnostic)
{
	unsigned int column = 2;
	unsigned int row = 2;
	if (active_framebuffer == NULL || framebuffer_columns < 24U ||
	    framebuffer_rows < 5U)
		return;
	fill(0x880000U);
	panic_draw_text(&column, &row, "*** KERNEL PANIC ***\n\n");
	panic_draw_text(&column, &row,
	    "The system halted. Recent kernel diagnostics:\n\n");
	if (diagnostic != NULL && diagnostic[0] != '\0')
		panic_draw_text(&column, &row, diagnostic);
	else
		panic_draw_text(&column, &row, "No diagnostic text was recorded.\n");
	panic_draw_text(&column, &row,
	    "\n\nThe same diagnostic was written to the serial console.\n");
}
