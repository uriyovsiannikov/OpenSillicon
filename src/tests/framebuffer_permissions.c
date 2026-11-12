#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kernel/framebuffer_device.h"
#include "kernel/process.h"
#include "kernel/tty.h"
#include "kernel/vmm.h"
#include "kernel/terminal.h"

static uint64_t pid = 300;
static uint32_t uid = 300;
static bool have_credentials = true;
static size_t copies;
static size_t input_reads, mode_changes;
uint64_t timer_ticks(void) { return 0; }
bool ps2_keyboard_available(void) { return true; }
bool ps2_keyboard_poll(struct terminal_key_event *event) { (void)event; return false; }
void terminal_key_event(struct terminal_key_event event) { (void)event; }
bool terminal_raw_mode(void) { return false; }
void terminal_set_raw_mode(bool enabled) { (void)enabled; mode_changes++; }
int terminal_read(char *buffer, size_t length, size_t *read_length)
{
	assert(length > 0);
	input_reads++;
	buffer[0] = 'A';
	*read_length = 1;
	return 0;
}
uint64_t process_current_pid(void) { return pid; }
bool process_current_credentials(struct os_credentials *credentials)
{
	if (!have_credentials) return false;
	*credentials = (struct os_credentials){.effective_uid = uid};
	return true;
}
struct address_space *vmm_active_address_space(void)
{
	return (struct address_space *)1;
}
bool vmm_validate_user_read(const struct address_space *space,
    uint64_t source, size_t length)
{
	(void)space; (void)length; return source != 0;
}
bool vmm_copy_from_user(const struct address_space *space, void *destination,
    uint64_t source, size_t length)
{
	(void)space;
	copies++;
	memcpy(destination, (const void *)(uintptr_t)source, length);
	return true;
}
bool terminal_switch(unsigned int terminal) { (void)terminal; return true; }
unsigned int terminal_active(void) { return 0; }

static void
input(bool allowed)
{
	char byte;
	size_t length = 100, before = input_reads, modes_before = mode_changes;
	assert(tty_read(&byte, 1, &length) == (allowed ? 0 : -1));
	assert(length == (allowed ? 1U : 0U));
	assert(input_reads == before + (allowed ? 1U : 0U));
	assert(tty_set_raw_mode(true) == (allowed ? 0 : -1));
	assert(mode_changes == modes_before + (allowed ? 1U : 0U));
}

static void
present(bool allowed)
{
	uint32_t source[4] = {1, 2, 3, 4};
	struct os_framebuffer_rect rect = {
		.source = (uintptr_t)source, .width = 2, .height = 2
	};
	size_t before = copies;
	assert(framebuffer_device_present((uintptr_t)source, sizeof(source)) ==
	    (allowed ? 0 : -1));
	assert(framebuffer_device_present_rect(&rect) == (allowed ? 0 : -1));
	assert(allowed ? copies > before : copies == before);
}

int main(void)
{
	uint32_t pixels[4] = {0};
	struct limine_framebuffer fb = {
		.address = pixels, .width = 2, .height = 2, .pitch = 8, .bpp = 32
	};
	assert(framebuffer_device_init(&fb));
	present(false);
	input(true);
	assert(pixels[0] == 0);
	uid = 0;
	present(true);
	uid = 300;
	assert(tty_console_framebuffer_set(false, pid));
	present(true);
	input(false);
	uid = 0;
	input(true);
	uid = 300;
	pid = 301;
	present(false);
	input(false);
	assert(tty_console_input_set(true, pid, 300));
	present(true);
	input(true);
	have_credentials = false;
	present(false);
	input(false);
	have_credentials = true;
	tty_console_framebuffer_owner_exit(300);
	present(false);
	input(true);
	uid = 0;
	present(true);
	puts("Framebuffer permission tests passed");
	return 0;
}
