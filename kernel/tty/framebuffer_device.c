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

int
framebuffer_device_present_rect(const struct os_framebuffer_rect *rect)
{
	uint32_t staging[FRAMEBUFFER_COPY_CHUNK / sizeof(uint32_t)];
	uint8_t *destination;
	struct address_space *space = vmm_active_address_space();
	uint64_t x_offset, row_bytes;

	if (active_framebuffer == NULL || space == NULL)
		return -FRAMEBUFFER_DEVICE_ENODEV;
	if (rect == NULL || rect->width == 0 || rect->height == 0 ||
	    rect->x > active_framebuffer->width ||
	    rect->width > active_framebuffer->width - rect->x ||
	    rect->y > active_framebuffer->height ||
	    rect->height > active_framebuffer->height - rect->y)
		return -FRAMEBUFFER_DEVICE_EINVAL;
	if (active_framebuffer->bpp != 32)
		return -FRAMEBUFFER_DEVICE_EINVAL;
	x_offset = (uint64_t)rect->x * sizeof(uint32_t);
	row_bytes = (uint64_t)rect->width * sizeof(uint32_t);
	if (x_offset > active_framebuffer->pitch ||
	    row_bytes > active_framebuffer->pitch - x_offset ||
	    rect->source == 0 || rect->source > UINT64_MAX - active_framebuffer_size)
		return -FRAMEBUFFER_DEVICE_EINVAL;
	destination = active_framebuffer->address;
	for (uint32_t row = 0; row < rect->height; row++) {
		uint64_t source_offset = ((uint64_t)rect->y + row) *
		    active_framebuffer->pitch + x_offset;
		uint64_t destination_offset = source_offset;
		uint64_t copied = 0;

		if (source_offset > active_framebuffer_size ||
		    row_bytes > active_framebuffer_size - source_offset ||
		    rect->source > UINT64_MAX - source_offset ||
		    !vmm_validate_user_read(space, rect->source + source_offset,
		    (size_t)row_bytes))
			return -14;
		while (copied < row_bytes) {
			size_t amount = (size_t)(row_bytes - copied);
			if (amount > sizeof(staging))
				amount = sizeof(staging);
			if (!vmm_copy_from_user(space, staging,
			    rect->source + source_offset + copied, amount))
				return -14;
			if (((uintptr_t)destination + destination_offset + copied) %
			    sizeof(uint64_t) == 0) {
				volatile uint64_t *pixels = (volatile uint64_t *)
				    (destination + destination_offset + copied);
				/* Read staging through memcpy to avoid alignment and
				 * aliasing assumptions about its uint32_t elements. */
				for (size_t index = 0; index < amount / sizeof(uint64_t);
				    index++) {
					uint64_t pair;
					__builtin_memcpy(&pair, (const uint8_t *)staging +
					    index * sizeof(pair), sizeof(pair));
					pixels[index] = pair;
				}
				if (amount % sizeof(uint64_t) != 0)
					*(volatile uint32_t *)(destination +
					    destination_offset + copied + amount - 4) =
					    staging[amount / sizeof(uint32_t) - 1];
			} else if (((uintptr_t)destination + destination_offset + copied) %
			    sizeof(uint32_t) == 0) {
				volatile uint32_t *pixels = (volatile uint32_t *)
				    (destination + destination_offset + copied);
				for (size_t index = 0; index < amount / sizeof(uint32_t);
				    index++)
					pixels[index] = staging[index];
			} else {
				const uint8_t *bytes = (const uint8_t *)staging;
				volatile uint8_t *pixels = destination +
				    destination_offset + copied;
				for (size_t index = 0; index < amount; index++)
					pixels[index] = bytes[index];
			}
			copied += amount;
		}
	}
	return 0;
}
