#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/e1000.h"

struct fake_registers {
	uint32_t control;
	uint32_t status;
	bool reset_clears;
	uint32_t last_write_offset;
	uint32_t last_write_value;
};

static bool
fake_read(void *context, uint32_t offset, uint32_t *value)
{
	struct fake_registers *registers = context;

	if ((offset & 3U) != 0 || value == NULL)
		return false;
	if (offset == E1000_REG_CTRL)
		*value = registers->control;
	else if (offset == E1000_REG_STATUS)
		*value = registers->status;
	else
		return false;
	return true;
}

static bool
fake_write(void *context, uint32_t offset, uint32_t value)
{
	struct fake_registers *registers = context;

	if (offset != E1000_REG_CTRL)
		return false;
	registers->last_write_offset = offset;
	registers->last_write_value = value;
	registers->control = registers->reset_clears ?
	    value & ~E1000_CTRL_RST : value;
	return true;
}

int
main(void)
{
	struct fake_registers registers = {
		.control = 0x00000001,
		.status = 0x00080083,
		.reset_clears = true
	};
	struct e1000_register_ops ops = {
		.read32 = fake_read,
		.write32 = fake_write,
		.context = &registers
	};
	bool timed_out;

	assert(e1000_register_access_valid(0, 4, 0x20000));
	assert(e1000_register_access_valid(0x1fffc, 4, 0x20000));
	assert(!e1000_register_access_valid(2, 4, 0x20000));
	assert(!e1000_register_access_valid(0x1fffd, 4, 0x20000));
	assert(!e1000_register_access_valid(0x20000, 4, 0x20000));
	assert(!e1000_register_access_valid(0, 8, 0x20000));

	assert(e1000_reset_and_validate(&ops, 4, &timed_out));
	assert(!timed_out);
	assert(registers.last_write_offset == E1000_REG_CTRL);
	assert(registers.last_write_value == (0x00000001 | E1000_CTRL_RST));
	assert(registers.control == 1);

	registers.reset_clears = false;
	assert(!e1000_reset_and_validate(&ops, 4, &timed_out));
	assert(timed_out);

	registers.control = UINT32_MAX;
	assert(!e1000_reset_and_validate(&ops, 4, &timed_out));
	assert(!timed_out);
	registers.control = 0;
	registers.status = UINT32_MAX;
	registers.reset_clears = true;
	assert(!e1000_reset_and_validate(&ops, 4, &timed_out));
	assert(!timed_out);
	assert(!e1000_reset_and_validate(&ops, 0, &timed_out));
	puts("e1000 reset completion, timeout, and register validation tests passed");
	return 0;
}
