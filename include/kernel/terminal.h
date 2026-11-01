#ifndef KERNEL_TERMINAL_H
#define KERNEL_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TERMINAL_COUNT 4U
#define TERMINAL_MAX_COLUMNS 160U
#define TERMINAL_MAX_ROWS 64U
#define TERMINAL_LINE_CAPACITY 256U
#define TERMINAL_INPUT_CAPACITY 1024U

struct terminal_cell {
	uint8_t character;
	uint8_t foreground;
	uint8_t background;
};

struct terminal_key_event {
	uint8_t scancode;
	bool extended;
	bool pressed;
};

typedef void (*terminal_draw_cell)(void *context, unsigned int terminal,
	unsigned int column, unsigned int row, struct terminal_cell cell);

bool terminal_init(unsigned int columns, unsigned int rows,
	terminal_draw_cell draw_cell, void *context);
void terminal_write(const char *buffer, size_t length);
int terminal_read(char *buffer, size_t length, size_t *read_length);
void terminal_set_raw_mode(bool enabled);
bool terminal_raw_mode(void);
void terminal_key_event(struct terminal_key_event event);
bool terminal_switch(unsigned int terminal);
unsigned int terminal_active(void);
struct terminal_cell terminal_cell_at(unsigned int terminal,
	unsigned int column, unsigned int row);

#endif
