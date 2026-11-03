#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/framebuffer_device.h"
#include "kernel/vmm.h"

static struct address_space space;
static unsigned char source[128];

struct address_space *vmm_active_address_space(void) { return &space; }
bool
vmm_validate_user_read(const struct address_space *s, uint64_t address, size_t size)
{
	return s == &space && address >= (uintptr_t)source &&
	    address - (uintptr_t)source <= sizeof(source) &&
	    size <= sizeof(source) - (address - (uintptr_t)source);
}
bool
vmm_copy_from_user(const struct address_space *s, void *buffer,
	uint64_t address, size_t size)
{
	if (!vmm_validate_user_read(s, address, size)) return false;
	memcpy(buffer, (const void *)(uintptr_t)address, size);
	return true;
}

int
main(void)
{
	_Alignas(4) unsigned char pixels[128];
	struct limine_framebuffer fb = {
		.address = pixels, .width = 6, .height = 4, .pitch = 32, .bpp = 32
	};
	struct os_framebuffer_rect rect = {
		.source = (uintptr_t)source, .x = 1, .y = 1, .width = 3, .height = 2
	};
	assert(framebuffer_device_present_rect(&rect) == -19);
	assert(framebuffer_device_init(&fb));
	for (size_t i = 0; i < sizeof(source); i++) source[i] = (unsigned char)i;
	memset(pixels, 0xa5, sizeof(pixels));
	assert(framebuffer_device_present_rect(&rect) == 0);
	for (size_t y = 0; y < 4; y++)
		for (size_t x = 0; x < 32; x++)
			assert(pixels[y * 32 + x] ==
			    (y >= 1 && y < 3 && x >= 4 && x < 16 ?
			    source[y * 32 + x] : 0xa5));
	rect.x = 5;
	assert(framebuffer_device_present_rect(&rect) == -22);
	rect.x = 1;
	rect.width = 0;
	assert(framebuffer_device_present_rect(&rect) == -22);
	rect.width = 3;
	rect.source = UINT64_MAX;
	assert(framebuffer_device_present_rect(&rect) == -22);
	rect.source = 1;
	assert(framebuffer_device_present_rect(&rect) == -14);
	rect.source = (uintptr_t)source;
	fb.bpp = 24;
	assert(framebuffer_device_present_rect(&rect) == -22);
	fb.bpp = 32;
	/* An unusual pitch uses the byte-store fallback without touching padding. */
	fb.pitch = 31;
	assert(framebuffer_device_init(&fb));
	memset(pixels, 0xa5, sizeof(pixels));
	assert(framebuffer_device_present_rect(&rect) == 0);
	assert(memcmp(pixels + 35, source + 35, 12) == 0);
	assert(pixels[34] == 0xa5 && pixels[47] == 0xa5);
	puts("Framebuffer damage rectangle, pitch, bounds and user-copy tests passed");
	return 0;
}
