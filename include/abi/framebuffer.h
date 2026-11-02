#ifndef ABI_FRAMEBUFFER_H
#define ABI_FRAMEBUFFER_H

#include <stdint.h>

struct os_framebuffer_info {
	uint64_t width;
	uint64_t height;
	uint64_t pitch;
	uint64_t size;
	uint16_t bits_per_pixel;
	uint8_t red_mask_size;
	uint8_t red_mask_shift;
	uint8_t green_mask_size;
	uint8_t green_mask_shift;
	uint8_t blue_mask_size;
	uint8_t blue_mask_shift;
	uint8_t reserved[6];
};

#endif
