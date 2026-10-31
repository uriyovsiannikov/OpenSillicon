#include <stddef.h>
#include <stdint.h>

#include "kernel/framebuffer.h"
#include "kernel/terminal.h"

#include "font8x8/font8x8_basic.h"

#define FONT_WIDTH 8U
#define FONT_HEIGHT 16U

static const struct limine_framebuffer *active_framebuffer;
static unsigned int framebuffer_columns;
static unsigned int framebuffer_rows;
static const uint32_t palette[8] = {
	0x000000, 0xaa0000, 0x00aa00, 0xaa5500,
	0x0000aa, 0xaa00aa, 0x00aaaa, 0xaaaaaa
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

	(void)context;
	if (active_framebuffer == NULL || terminal != terminal_active() ||
	    column >= framebuffer_columns || row >= framebuffer_rows)
		return;
	foreground = palette[cell.foreground & 7U];
	background = palette[cell.background & 7U];
	origin_x = (uint64_t)column * FONT_WIDTH;
	origin_y = (uint64_t)row * FONT_HEIGHT;
	for (glyph_row = 0; glyph_row < 8; glyph_row++) {
		uint8_t bits = font8x8_basic[cell.character][glyph_row];
		unsigned int vertical;

		for (vertical = 0; vertical < 2; vertical++) {
			for (x = 0; x < FONT_WIDTH; x++) {
				uint32_t color = (bits & (1U << x)) != 0 ? foreground :
				    background;

				put_pixel(origin_x + x, origin_y + glyph_row * 2 + vertical,
				    color);
			}
		}
	}
}
