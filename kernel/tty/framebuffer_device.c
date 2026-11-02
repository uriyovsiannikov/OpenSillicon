#include <stddef.h>
#include <stdint.h>

#include "kernel/framebuffer_device.h"
#include "kernel/vmm.h"

#define FRAMEBUFFER_DEVICE_EINVAL 22
#define FRAMEBUFFER_DEVICE_ENODEV 19
#define FRAMEBUFFER_COPY_CHUNK 4096U

static const struct limine_framebuffer *active_framebuffer;
static uint64_t active_framebuffer_size;

bool
framebuffer_device_init(const struct limine_framebuffer *framebuffer)
{
	uint64_t size;

	active_framebuffer = NULL;
	active_framebuffer_size = 0;
	if (framebuffer == NULL || framebuffer->address == NULL ||
	    framebuffer->width == 0 || framebuffer->height == 0 ||
	    framebuffer->pitch == 0 ||
	    framebuffer->height > UINT64_MAX / framebuffer->pitch)
		return false;
	size = framebuffer->height * framebuffer->pitch;
	if (size == 0 || size > SIZE_MAX)
		return false;
	active_framebuffer = framebuffer;
	active_framebuffer_size = size;
	return true;
}

bool
framebuffer_device_info(struct os_framebuffer_info *info)
{
	if (info == NULL || active_framebuffer == NULL)
		return false;
	*info = (struct os_framebuffer_info){
		.width = active_framebuffer->width,
		.height = active_framebuffer->height,
		.pitch = active_framebuffer->pitch,
		.size = active_framebuffer_size,
		.bits_per_pixel = active_framebuffer->bpp,
		.red_mask_size = active_framebuffer->red_mask_size,
		.red_mask_shift = active_framebuffer->red_mask_shift,
		.green_mask_size = active_framebuffer->green_mask_size,
		.green_mask_shift = active_framebuffer->green_mask_shift,
		.blue_mask_size = active_framebuffer->blue_mask_size,
		.blue_mask_shift = active_framebuffer->blue_mask_shift,
		.reserved = { 0 }
	};
	return true;
}

int
framebuffer_device_present(uint64_t user_address, size_t length)
{
	uint8_t staging[FRAMEBUFFER_COPY_CHUNK];
	uint8_t *destination;
	struct address_space *space = vmm_active_address_space();
	size_t offset = 0;

	if (active_framebuffer == NULL || space == NULL)
		return -FRAMEBUFFER_DEVICE_ENODEV;
	if (length != active_framebuffer_size || user_address == 0 ||
	    user_address > UINT64_MAX - length)
		return -FRAMEBUFFER_DEVICE_EINVAL;
	if (!vmm_validate_user_read(space, user_address, length))
		return -14;
	destination = active_framebuffer->address;
	while (offset < length) {
		size_t amount = length - offset;
		if (amount > sizeof(staging))
			amount = sizeof(staging);
		if (!vmm_copy_from_user(space, staging, user_address + offset,
		    amount))
			return -14;
		for (size_t index = 0; index < amount; index++)
			destination[offset + index] = staging[index];
		offset += amount;
	}
	return 0;
}
