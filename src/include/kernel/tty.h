#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <stdbool.h>
#include <stddef.h>

#include "kernel/boot.h"

bool tty_init(const struct limine_framebuffer *framebuffer);
void tty_boot_splash_end(void);
bool tty_framebuffer_available(void);
bool tty_keyboard_available(void);
bool tty_dimensions(unsigned short *rows, unsigned short *columns);
int tty_read(void *buffer, size_t length, size_t *read_length);
bool tty_write(const void *buffer, size_t length);
bool tty_console_framebuffer_set(bool enabled, uint64_t owner_pid);
bool tty_console_input_set(bool acquire, uint64_t owner_pid, uint64_t parent_pid);
bool tty_console_input_owned(uint64_t owner_pid);
bool tty_console_graphics_owned(uint64_t owner_pid);
void tty_console_framebuffer_owner_exit(uint64_t owner_pid);
bool tty_set_raw_mode(bool enabled);
bool tty_raw_mode(void);
_Noreturn void tty_console_loop(void);

#endif
