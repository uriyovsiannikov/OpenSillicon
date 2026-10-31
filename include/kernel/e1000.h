#ifndef KERNEL_E1000_H
#define KERNEL_E1000_H

#include <stdbool.h>
#include <stdint.h>

#include "arch/x86_64/pci.h"

enum e1000_lifecycle_state {
	E1000_STATE_ABSENT = 0,
	E1000_STATE_DISCOVERED,
	E1000_STATE_RESOURCES_VALIDATED,
	E1000_STATE_MAPPED,
	E1000_STATE_RESET_IN_PROGRESS,
	E1000_STATE_INITIALIZED,
	E1000_STATE_FAILED,
	E1000_STATE_STOPPED
};

enum e1000_failure {
	E1000_FAILURE_NONE = 0,
	E1000_FAILURE_DUPLICATE,
	E1000_FAILURE_DMA_CONTRACT,
	E1000_FAILURE_PROFILE,
	E1000_FAILURE_PCI_CONFIG,
	E1000_FAILURE_MAP,
	E1000_FAILURE_REGISTER,
	E1000_FAILURE_RESET_TIMEOUT,
	E1000_FAILURE_POST_RESET
};

/* Singleton for the explicitly supported QEMU 82540EM profile. This stage
 * only identifies, maps, resets, and validates registers; no DMA or I/O path
 * is configured. */
bool e1000_discover(const struct pci_device_info *device);
bool e1000_initialize(void);
enum e1000_lifecycle_state e1000_state(void);
enum e1000_failure e1000_last_failure(void);

/* Register definitions shared with the isolated reset-state tests. */
#define E1000_REG_CTRL 0x0000U
#define E1000_REG_STATUS 0x0008U
#define E1000_CTRL_RST (1U << 26)
#define E1000_RESET_POLL_LIMIT 1000000U
bool e1000_register_access_valid(uint32_t offset, size_t width,
	size_t aperture_size);

typedef bool (*e1000_read32_fn)(void *context, uint32_t offset,
	uint32_t *value);
typedef bool (*e1000_write32_fn)(void *context, uint32_t offset,
	uint32_t value);

struct e1000_register_ops {
	e1000_read32_fn read32;
	e1000_write32_fn write32;
	void *context;
};

/* Testable bounded reset core; successful validation leaves reset deasserted
 * and verifies CTRL/STATUS are not floating all-ones values. */
bool e1000_reset_and_validate(const struct e1000_register_ops *ops,
	uint32_t poll_limit, bool *timed_out);

#endif
