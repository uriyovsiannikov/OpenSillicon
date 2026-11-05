#ifndef KERNEL_TPM2_CRB_H
#define KERNEL_TPM2_CRB_H

#include <stddef.h>
#include <stdint.h>

/* PTP 1.07 PC Client CRB register offsets, relative to locality 0. */
#define TPM2_CRB_REG_LOC_STATE 0x00U
#define TPM2_CRB_REG_LOC_CTRL  0x08U
#define TPM2_CRB_REG_LOC_STS   0x0cU
#define TPM2_CRB_REG_CTRL_REQ  0x40U
#define TPM2_CRB_REG_CTRL_STS  0x44U
#define TPM2_CRB_REG_START     0x4cU

#define TPM2_CRB_LOC_STATE_ASSIGNED (1U << 1)
#define TPM2_CRB_LOC_STATE_VALID    (1U << 7)
#define TPM2_CRB_LOC_CTRL_REQUEST   (1U << 0)
#define TPM2_CRB_LOC_CTRL_RELEASE   (1U << 1)
#define TPM2_CRB_LOC_STS_GRANTED    (1U << 0)
#define TPM2_CRB_CTRL_CMD_READY     (1U << 0)
#define TPM2_CRB_CTRL_GO_IDLE       (1U << 1)
#define TPM2_CRB_CTRL_STS_ERROR     (1U << 0)
#define TPM2_CRB_CTRL_STS_IDLE      (1U << 1)
#define TPM2_CRB_START_INVOKE       (1U << 0)

#define TPM2_CRB_MAX_BUFFER_SIZE 4096U
#define TPM2_CRB_MAX_POLLS 32U

enum tpm2_crb_status {
	TPM2_CRB_OK = 0,
	TPM2_CRB_INVALID_ARGUMENT,
	TPM2_CRB_INVALID_STATE,
	TPM2_CRB_INVALID_REGISTER,
	TPM2_CRB_INVALID_LENGTH,
	TPM2_CRB_TIMEOUT,
	TPM2_CRB_BACKEND_ERROR,
	TPM2_CRB_DEVICE_ERROR
};

enum tpm2_crb_state {
	TPM2_CRB_STATE_UNINITIALIZED = 0,
	TPM2_CRB_STATE_IDLE,
	TPM2_CRB_STATE_LOCALITY,
	TPM2_CRB_STATE_READY,
	TPM2_CRB_STATE_RUNNING,
	TPM2_CRB_STATE_COMPLETE,
	TPM2_CRB_STATE_FAULT
};

/* Callbacks are the only register/buffer access boundary. read_response must
 * leave its destination unchanged on error. The host fake is the sole backend
 * in this phase; a future MMIO backend needs separate review. Buffer callbacks
 * copy bytes and never transfer ownership of caller memory.
 */
struct tpm2_crb_backend {
	int (*read32)(void *context, uint32_t offset, uint32_t *value);
	int (*write32)(void *context, uint32_t offset, uint32_t value);
	int (*write_command)(void *context, const uint8_t *bytes, size_t length);
	int (*read_response)(void *context, uint8_t *bytes, size_t capacity,
		size_t *actual_length);
};

struct tpm2_crb {
	const struct tpm2_crb_backend *backend;
	void *backend_context;
	size_t command_capacity;
	size_t response_capacity;
	enum tpm2_crb_state state;
};

enum tpm2_crb_status tpm2_crb_init(struct tpm2_crb *crb,
	const struct tpm2_crb_backend *backend, void *backend_context,
	size_t command_capacity, size_t response_capacity);
enum tpm2_crb_status tpm2_crb_read_register(struct tpm2_crb *crb,
	uint32_t offset, uint32_t *value);
enum tpm2_crb_status tpm2_crb_write_register(struct tpm2_crb *crb,
	uint32_t offset, uint32_t value);
enum tpm2_crb_status tpm2_crb_request_locality(struct tpm2_crb *crb);
enum tpm2_crb_status tpm2_crb_command_ready(struct tpm2_crb *crb);
enum tpm2_crb_status tpm2_crb_start(struct tpm2_crb *crb,
	const uint8_t *command, size_t command_length);
enum tpm2_crb_status tpm2_crb_read_response(struct tpm2_crb *crb,
	uint8_t *response, size_t capacity, size_t *actual_length);
enum tpm2_crb_status tpm2_crb_go_idle(struct tpm2_crb *crb);
enum tpm2_crb_status tpm2_crb_release_locality(struct tpm2_crb *crb);

#endif
