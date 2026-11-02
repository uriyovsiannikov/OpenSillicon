#ifndef ARCH_X86_64_PS2_KEYBOARD_H
#define ARCH_X86_64_PS2_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/terminal.h"

struct os_mouse_event;

struct ps2_keyboard_decoder {
	bool extended;
	uint8_t pause_remaining;
};

enum ps2_keyboard_error {
	PS2_KEYBOARD_OK,
	PS2_KEYBOARD_CONTROLLER_TIMEOUT,
	PS2_KEYBOARD_CONFIG_READ_TIMEOUT,
	PS2_KEYBOARD_CONFIG_WRITE_TIMEOUT,
	PS2_KEYBOARD_PORT_ENABLE_FAILED,
	PS2_KEYBOARD_SCAN_ENABLE_FAILED
};

void ps2_keyboard_decoder_init(struct ps2_keyboard_decoder *decoder);
bool ps2_keyboard_decode_byte(struct ps2_keyboard_decoder *decoder,
	uint8_t byte, struct terminal_key_event *event);
bool ps2_keyboard_init(void);
bool ps2_keyboard_available(void);
enum ps2_keyboard_error ps2_keyboard_last_error(void);
bool ps2_keyboard_poll(struct terminal_key_event *event);
bool ps2_keyboard_event_poll(struct terminal_key_event *event);
bool ps2_mouse_init(void);
bool ps2_mouse_available(void);
bool ps2_mouse_event_poll(struct os_mouse_event *event);

#endif
