#ifndef ABI_INPUT_H
#define ABI_INPUT_H

#include <stdint.h>

/* Synthetic Set 1 code used for Pause (which has no normal break code). */
#define OS_KEYBOARD_PAUSE_SCANCODE 0x7fU

struct os_keyboard_event {
	uint8_t scancode;
	uint8_t extended;
	uint8_t pressed;
	uint8_t reserved;
};

struct os_mouse_event {
	int16_t dx;
	int16_t dy;
	uint8_t buttons;
	uint8_t reserved[3];
};

#endif
