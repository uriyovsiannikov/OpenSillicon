#include <assert.h>
#include <stdio.h>

/* Include the driver to exercise its packet decoder without hardware I/O.
 * Unused hardware entry points are discarded by the linker. */
#include "../kernel/drivers/input/ps2_keyboard.c"

int
main(void)
{
	mouse_capture_enabled = true;
	for (int x = -255; x <= 255; x++)
		for (int y = -255; y <= 255; y++) {
			uint8_t flags = 0x08U | (x < 0 ? 0x10U : 0) |
			    (y < 0 ? 0x20U : 0) | 1U;
			mouse_event_count = 0;
			mouse_event_head = 0;
			ps2_mouse_decode_byte(flags);
			ps2_mouse_decode_byte((uint8_t)x);
			ps2_mouse_decode_byte((uint8_t)y);
			assert(mouse_event_count == 1);
			assert(mouse_event_queue[0].dx == x);
			assert(mouse_event_queue[0].dy == -y);
			assert(mouse_event_queue[0].buttons == 1);
		}
	puts("PS/2 mouse: signed 9-bit movement range passed");
	return 0;
}
