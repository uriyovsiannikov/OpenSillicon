#include <string.h>

#include "kernel/tpm2_crb.h"

static int
valid_register(uint32_t offset)
{
	switch (offset) {
	case TPM2_CRB_REG_LOC_STATE:
	case TPM2_CRB_REG_LOC_CTRL:
	case TPM2_CRB_REG_LOC_STS:
	case TPM2_CRB_REG_CTRL_REQ:
	case TPM2_CRB_REG_CTRL_STS:
	case TPM2_CRB_REG_START:
		return 1;
	default:
		return 0;
	}
}

static enum tpm2_crb_status
backend_read(struct tpm2_crb *crb, uint32_t offset, uint32_t *value)
{
	if (crb == NULL || crb->backend == NULL || value == NULL ||
	    !valid_register(offset))
		return TPM2_CRB_INVALID_REGISTER;
	if (crb->backend->read32(crb->backend_context, offset, value) != 0)
		return TPM2_CRB_BACKEND_ERROR;
	return TPM2_CRB_OK;
}

static enum tpm2_crb_status
backend_write(struct tpm2_crb *crb, uint32_t offset, uint32_t value)
{
	if (crb == NULL || crb->backend == NULL || !valid_register(offset))
		return TPM2_CRB_INVALID_REGISTER;
	if (crb->backend->write32(crb->backend_context, offset, value) != 0)
		return TPM2_CRB_BACKEND_ERROR;
	return TPM2_CRB_OK;
}

static enum tpm2_crb_status
fault_on_error(struct tpm2_crb *crb, enum tpm2_crb_status status)
{
	if (status != TPM2_CRB_OK)
		crb->state = TPM2_CRB_STATE_FAULT;
	return status;
}

static enum tpm2_crb_status
wait_register(struct tpm2_crb *crb, uint32_t offset, uint32_t mask,
	uint32_t expected)
{
	for (unsigned int i = 0; i < TPM2_CRB_MAX_POLLS; i++) {
		uint32_t value;
		enum tpm2_crb_status status = backend_read(crb, offset, &value);

		if (status != TPM2_CRB_OK)
			return status;
		if ((value & mask) == expected)
			return TPM2_CRB_OK;
	}
	return TPM2_CRB_TIMEOUT;
}

enum tpm2_crb_status
tpm2_crb_init(struct tpm2_crb *crb, const struct tpm2_crb_backend *backend,
	void *backend_context, size_t command_capacity, size_t response_capacity)
{
	if (crb == NULL || backend == NULL || backend->read32 == NULL ||
	    backend->write32 == NULL || backend->write_command == NULL ||
	    backend->read_response == NULL || command_capacity == 0 ||
	    response_capacity == 0 || command_capacity > TPM2_CRB_MAX_BUFFER_SIZE ||
	    response_capacity > TPM2_CRB_MAX_BUFFER_SIZE)
		return TPM2_CRB_INVALID_ARGUMENT;
	*crb = (struct tpm2_crb){
		.backend = backend,
		.backend_context = backend_context,
		.command_capacity = command_capacity,
		.response_capacity = response_capacity,
		.state = TPM2_CRB_STATE_IDLE
	};
	return TPM2_CRB_OK;
}

enum tpm2_crb_status
tpm2_crb_read_register(struct tpm2_crb *crb, uint32_t offset, uint32_t *value)
{
	if (crb == NULL || crb->state == TPM2_CRB_STATE_UNINITIALIZED ||
	    value == NULL)
		return TPM2_CRB_INVALID_ARGUMENT;
	return backend_read(crb, offset, value);
}

enum tpm2_crb_status
tpm2_crb_write_register(struct tpm2_crb *crb, uint32_t offset, uint32_t value)
{
	if (crb == NULL || crb->state == TPM2_CRB_STATE_UNINITIALIZED)
		return TPM2_CRB_INVALID_ARGUMENT;
	/* Expose only defined request registers and single documented bits. */
	if ((offset == TPM2_CRB_REG_LOC_CTRL &&
	    ((value & ~(TPM2_CRB_LOC_CTRL_REQUEST | TPM2_CRB_LOC_CTRL_RELEASE)) != 0 ||
	    value == (TPM2_CRB_LOC_CTRL_REQUEST | TPM2_CRB_LOC_CTRL_RELEASE))) ||
	    (offset == TPM2_CRB_REG_CTRL_REQ &&
	    ((value & ~(TPM2_CRB_CTRL_CMD_READY | TPM2_CRB_CTRL_GO_IDLE)) != 0 ||
	    value == (TPM2_CRB_CTRL_CMD_READY | TPM2_CRB_CTRL_GO_IDLE))) ||
	    (offset == TPM2_CRB_REG_START &&
	    (value & ~TPM2_CRB_START_INVOKE) != 0) ||
	    (offset != TPM2_CRB_REG_LOC_CTRL && offset != TPM2_CRB_REG_CTRL_REQ &&
	    offset != TPM2_CRB_REG_START))
		return TPM2_CRB_INVALID_REGISTER;
	return backend_write(crb, offset, value);
}

enum tpm2_crb_status
tpm2_crb_request_locality(struct tpm2_crb *crb)
{
	enum tpm2_crb_status status;
	uint32_t state;

	if (crb == NULL || crb->state != TPM2_CRB_STATE_IDLE)
		return TPM2_CRB_INVALID_STATE;
	status = tpm2_crb_write_register(crb, TPM2_CRB_REG_LOC_CTRL,
	    TPM2_CRB_LOC_CTRL_REQUEST);
	if (status != TPM2_CRB_OK)
		return fault_on_error(crb, status);
	for (unsigned int i = 0; i < TPM2_CRB_MAX_POLLS; i++) {
		uint32_t ownership;
		status = backend_read(crb, TPM2_CRB_REG_LOC_STATE, &state);
		if (status != TPM2_CRB_OK)
			return fault_on_error(crb, status);
		status = backend_read(crb, TPM2_CRB_REG_LOC_STS, &ownership);
		if (status != TPM2_CRB_OK)
			return fault_on_error(crb, status);
		if ((state & (TPM2_CRB_LOC_STATE_VALID |
		    TPM2_CRB_LOC_STATE_ASSIGNED)) ==
		    (TPM2_CRB_LOC_STATE_VALID | TPM2_CRB_LOC_STATE_ASSIGNED) &&
		    (ownership & TPM2_CRB_LOC_STS_GRANTED) != 0) {
			crb->state = TPM2_CRB_STATE_LOCALITY;
			return TPM2_CRB_OK;
		}
	}
	return fault_on_error(crb, TPM2_CRB_TIMEOUT);
}

enum tpm2_crb_status
tpm2_crb_command_ready(struct tpm2_crb *crb)
{
	enum tpm2_crb_status status;

	if (crb == NULL || crb->state != TPM2_CRB_STATE_LOCALITY)
		return TPM2_CRB_INVALID_STATE;
	status = tpm2_crb_write_register(crb, TPM2_CRB_REG_CTRL_REQ,
	    TPM2_CRB_CTRL_CMD_READY);
	if (status != TPM2_CRB_OK)
		return fault_on_error(crb, status);
	status = wait_register(crb, TPM2_CRB_REG_CTRL_REQ,
	    TPM2_CRB_CTRL_CMD_READY, 0);
	if (status == TPM2_CRB_OK)
		crb->state = TPM2_CRB_STATE_READY;
	return fault_on_error(crb, status);
}

enum tpm2_crb_status
tpm2_crb_start(struct tpm2_crb *crb, const uint8_t *command,
	size_t command_length)
{
	enum tpm2_crb_status status;
	uint32_t device_status;

	if (crb == NULL || crb->state != TPM2_CRB_STATE_READY)
		return TPM2_CRB_INVALID_STATE;
	if (command == NULL || command_length == 0 ||
	    command_length > crb->command_capacity)
		return TPM2_CRB_INVALID_LENGTH;
	if (crb->backend->write_command(crb->backend_context, command,
	    command_length) != 0)
		return fault_on_error(crb, TPM2_CRB_BACKEND_ERROR);
	status = backend_write(crb, TPM2_CRB_REG_START, TPM2_CRB_START_INVOKE);
	if (status != TPM2_CRB_OK)
		return fault_on_error(crb, status);
	crb->state = TPM2_CRB_STATE_RUNNING;
	for (unsigned int i = 0; i < TPM2_CRB_MAX_POLLS; i++) {
		uint32_t start;
		status = backend_read(crb, TPM2_CRB_REG_CTRL_STS, &device_status);
		if (status != TPM2_CRB_OK)
			return fault_on_error(crb, status);
		if ((device_status & TPM2_CRB_CTRL_STS_ERROR) != 0) {
			crb->state = TPM2_CRB_STATE_FAULT;
			return TPM2_CRB_DEVICE_ERROR;
		}
		status = backend_read(crb, TPM2_CRB_REG_START, &start);
		if (status != TPM2_CRB_OK)
			return fault_on_error(crb, status);
		if ((start & TPM2_CRB_START_INVOKE) == 0) {
			crb->state = TPM2_CRB_STATE_COMPLETE;
			return TPM2_CRB_OK;
		}
	}
	return fault_on_error(crb, TPM2_CRB_TIMEOUT);
}

enum tpm2_crb_status
tpm2_crb_read_response(struct tpm2_crb *crb, uint8_t *response,
	size_t capacity, size_t *actual_length)
{
	size_t length = 0;
	if (crb == NULL || crb->state != TPM2_CRB_STATE_COMPLETE ||
	    response == NULL || actual_length == NULL)
		return TPM2_CRB_INVALID_STATE;
	if (capacity == 0 || capacity > crb->response_capacity)
		return TPM2_CRB_INVALID_LENGTH;
	if (crb->backend->read_response(crb->backend_context, response, capacity,
	    &length) != 0)
		return TPM2_CRB_BACKEND_ERROR;
	if (length > capacity) {
		memset(response, 0, capacity);
		return TPM2_CRB_INVALID_LENGTH;
	}
	*actual_length = length;
	return TPM2_CRB_OK;
}

enum tpm2_crb_status
tpm2_crb_go_idle(struct tpm2_crb *crb)
{
	enum tpm2_crb_status status;
	if (crb == NULL || (crb->state != TPM2_CRB_STATE_READY &&
	    crb->state != TPM2_CRB_STATE_COMPLETE))
		return TPM2_CRB_INVALID_STATE;
	status = tpm2_crb_write_register(crb, TPM2_CRB_REG_CTRL_REQ,
	    TPM2_CRB_CTRL_GO_IDLE);
	if (status != TPM2_CRB_OK)
		return fault_on_error(crb, status);
	status = wait_register(crb, TPM2_CRB_REG_CTRL_REQ,
	    TPM2_CRB_CTRL_GO_IDLE, 0);
	if (status == TPM2_CRB_OK)
		crb->state = TPM2_CRB_STATE_LOCALITY;
	return fault_on_error(crb, status);
}

enum tpm2_crb_status
tpm2_crb_release_locality(struct tpm2_crb *crb)
{
	enum tpm2_crb_status status;
	if (crb == NULL || crb->state != TPM2_CRB_STATE_LOCALITY)
		return TPM2_CRB_INVALID_STATE;
	status = tpm2_crb_write_register(crb, TPM2_CRB_REG_LOC_CTRL,
	    TPM2_CRB_LOC_CTRL_RELEASE);
	if (status != TPM2_CRB_OK)
		return fault_on_error(crb, status);
	status = wait_register(crb, TPM2_CRB_REG_LOC_STS,
	    TPM2_CRB_LOC_STS_GRANTED, 0);
	if (status == TPM2_CRB_OK)
		crb->state = TPM2_CRB_STATE_IDLE;
	return fault_on_error(crb, status);
}
