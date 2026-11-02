#include "arch/x86_64/ps2_keyboard.h"
#include "abi/input.h"

void
ps2_keyboard_decoder_init(struct ps2_keyboard_decoder *decoder)
{
	if (decoder != NULL)
		*decoder = (struct ps2_keyboard_decoder){ 0 };
}

bool
ps2_keyboard_decode_byte(struct ps2_keyboard_decoder *decoder, uint8_t byte,
	struct terminal_key_event *event)
{
	if (decoder == NULL || event == NULL)
		return false;
	if (decoder->pause_remaining != 0) {
		decoder->pause_remaining--;
		return false;
	}
	if (byte == 0xe1) {
		decoder->extended = false;
		decoder->pause_remaining = 5;
		/* Set 1 has no ordinary make/break pair for Pause. Use the
		 * otherwise-invalid 0x7f code with extended=true as its make event. */
		*event = (struct terminal_key_event){
			.scancode = OS_KEYBOARD_PAUSE_SCANCODE,
			.extended = true,
			.pressed = true
		};
		return true;
	}
	if (byte == 0xe0) {
		decoder->extended = true;
		return false;
	}
	*event = (struct terminal_key_event){
		.scancode = (uint8_t)(byte & 0x7fU),
		.extended = decoder->extended,
		.pressed = (byte & 0x80U) == 0
	};
	decoder->extended = false;
	return true;
}
