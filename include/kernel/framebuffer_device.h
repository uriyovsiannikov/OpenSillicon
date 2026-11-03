#ifndef KERNEL_FRAMEBUFFER_DEVICE_H
#define KERNEL_FRAMEBUFFER_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "abi/framebuffer.h"
#include "limine.h"

bool framebuffer_device_init(const struct limine_framebuffer *framebuffer);
bool framebuffer_device_info(struct os_framebuffer_info *info);
int framebuffer_device_present(uint64_t user_address, size_t length);
int framebuffer_device_present_rect(const struct os_framebuffer_rect *rect);

#endif
