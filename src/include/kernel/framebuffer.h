#ifndef KERNEL_FRAMEBUFFER_H
#define KERNEL_FRAMEBUFFER_H

#include <stdbool.h>

#include "kernel/terminal.h"
#include "limine.h"

bool framebuffer_text_init(const struct limine_framebuffer *framebuffer,
	unsigned int *columns, unsigned int *rows);
void framebuffer_text_draw(void *context, unsigned int terminal,
	unsigned int column, unsigned int row, struct terminal_cell cell);
void framebuffer_panic_screen(const char *diagnostic);

#endif
