#include <stdint.h>
#include <stddef.h>

#include "kernel/e1000.h"

bool
e1000_register_access_valid(uint32_t offset, size_t width,
	size_t aperture_size)
{
	return width == sizeof(uint32_t) && (offset & 3U) == 0 &&
	    offset <= aperture_size && width <= aperture_size - offset;
}

bool
e1000_reset_and_validate(const struct e1000_register_ops *ops,
	uint32_t poll_limit, bool *timed_out)
{
	uint32_t control;
	uint32_t status;
	bool complete = false;

	if (timed_out != NULL)
		*timed_out = false;
	if (ops == NULL || ops->read32 == NULL || ops->write32 == NULL ||
	    poll_limit == 0 || !ops->read32(ops->context, E1000_REG_CTRL,
	    &control) || control == UINT32_MAX ||
	    !ops->write32(ops->context, E1000_REG_CTRL, control | E1000_CTRL_RST))
		return false;
	for (uint32_t poll = 0; poll < poll_limit; poll++) {
		if (!ops->read32(ops->context, E1000_REG_CTRL, &control) ||
		    control == UINT32_MAX)
			return false;
		if ((control & E1000_CTRL_RST) == 0) {
			complete = true;
			break;
		}
		__asm__ volatile ("pause" : : : "memory");
	}
	if (!complete) {
		if (timed_out != NULL)
			*timed_out = true;
		return false;
	}
	if (!ops->read32(ops->context, E1000_REG_STATUS, &status) ||
	    status == UINT32_MAX || (control & E1000_CTRL_RST) != 0)
		return false;
	return true;
}
