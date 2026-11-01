#include <string.h>

#include "tpm2_crb_fake.h"

static int
register_index(uint32_t offset)
{
	switch (offset) {
	case TPM2_CRB_REG_LOC_STATE: return 0;
	case TPM2_CRB_REG_LOC_CTRL: return 1;
	case TPM2_CRB_REG_LOC_STS: return 2;
	case TPM2_CRB_REG_CTRL_REQ: return 3;
	case TPM2_CRB_REG_CTRL_STS: return 4;
	case TPM2_CRB_REG_START: return 5;
	default: return -1;
	}
}

void
tpm2_crb_fake_init(struct tpm2_crb_fake *fake)
{
	if (fake == NULL)
		return;
	memset(fake, 0, sizeof(*fake));
	fake->registers[0] = TPM2_CRB_LOC_STATE_VALID;
	fake->registers[4] = TPM2_CRB_CTRL_STS_IDLE;
	fake->reported_response_length = SIZE_MAX;
}

static int
fake_read32(void *context, uint32_t offset, uint32_t *value)
{
	struct tpm2_crb_fake *fake = context;
	int index = register_index(offset);
	if (fake == NULL || value == NULL || index < 0 ||
	    (fake->fail_read && fake->fail_read_offset == offset))
		return -1;
	fake->read_count++;
	switch (offset) {
	case TPM2_CRB_REG_LOC_STATE:
		if (!fake->never_grant_locality && fake->locality_delay != 0 &&
		    --fake->locality_delay == 0)
			fake->registers[0] |= TPM2_CRB_LOC_STATE_ASSIGNED;
		break;
	case TPM2_CRB_REG_LOC_STS:
		if (!fake->never_grant_locality && fake->locality_delay == 0 &&
		    (fake->registers[0] & TPM2_CRB_LOC_STATE_ASSIGNED) != 0)
			fake->registers[2] |= TPM2_CRB_LOC_STS_GRANTED;
		break;
	case TPM2_CRB_REG_CTRL_REQ:
		if (!fake->never_ready && fake->ready_delay != 0 &&
		    --fake->ready_delay == 0)
			fake->registers[3] &= ~TPM2_CRB_CTRL_CMD_READY;
		if (!fake->never_idle && fake->idle_delay != 0 &&
		    --fake->idle_delay == 0)
			fake->registers[3] &= ~TPM2_CRB_CTRL_GO_IDLE;
		break;
	case TPM2_CRB_REG_START:
		if (!fake->never_complete && fake->start_delay != 0 &&
		    --fake->start_delay == 0) {
			fake->registers[5] &= ~TPM2_CRB_START_INVOKE;
			fake->registers[4] |= TPM2_CRB_CTRL_STS_IDLE;
		}
		break;
	default:
		break;
	}
	*value = fake->registers[index];
	return 0;
}

static int
fake_write32(void *context, uint32_t offset, uint32_t value)
{
	struct tpm2_crb_fake *fake = context;
	int index = register_index(offset);
	if (fake == NULL || index < 0 ||
	    (fake->fail_write && fake->fail_write_offset == offset))
		return -1;
	fake->write_count++;
	switch (offset) {
	case TPM2_CRB_REG_LOC_CTRL:
		fake->registers[1] = value;
		if ((value & TPM2_CRB_LOC_CTRL_REQUEST) != 0) {
			fake->registers[0] &= ~TPM2_CRB_LOC_STATE_ASSIGNED;
			fake->registers[2] &= ~TPM2_CRB_LOC_STS_GRANTED;
			if (!fake->never_grant_locality && fake->locality_delay == 0) {
				fake->registers[0] |= TPM2_CRB_LOC_STATE_ASSIGNED;
				fake->registers[2] |= TPM2_CRB_LOC_STS_GRANTED;
			}
		}
		if ((value & TPM2_CRB_LOC_CTRL_RELEASE) != 0) {
			fake->registers[0] &= ~TPM2_CRB_LOC_STATE_ASSIGNED;
			fake->registers[2] &= ~TPM2_CRB_LOC_STS_GRANTED;
		}
		break;
	case TPM2_CRB_REG_CTRL_REQ:
		fake->registers[3] = value;
		if ((value & TPM2_CRB_CTRL_CMD_READY) != 0) {
			fake->registers[4] &= ~TPM2_CRB_CTRL_STS_IDLE;
			if (!fake->never_ready && fake->ready_delay == 0)
				fake->registers[3] &= ~TPM2_CRB_CTRL_CMD_READY;
		}
		if ((value & TPM2_CRB_CTRL_GO_IDLE) != 0 &&
		    !fake->never_idle && fake->idle_delay == 0) {
			fake->registers[3] &= ~TPM2_CRB_CTRL_GO_IDLE;
			fake->registers[4] |= TPM2_CRB_CTRL_STS_IDLE;
		}
		break;
	case TPM2_CRB_REG_START:
		fake->registers[5] = value;
		if ((value & TPM2_CRB_START_INVOKE) != 0) {
			fake->registers[4] &= ~TPM2_CRB_CTRL_STS_IDLE;
			fake->saw_busy = 1;
			if (!fake->never_complete && fake->start_delay == 0) {
				fake->registers[5] &= ~TPM2_CRB_START_INVOKE;
				fake->registers[4] |= TPM2_CRB_CTRL_STS_IDLE;
			}
		}
		break;
	default:
		return -1;
	}
	return 0;
}

static int
fake_write_command(void *context, const uint8_t *bytes, size_t length)
{
	struct tpm2_crb_fake *fake = context;
	if (fake == NULL || bytes == NULL || length == 0 ||
	    length > sizeof(fake->command) || fake->fail_command)
		return -1;
	memcpy(fake->command, bytes, length);
	fake->command_length = length;
	return 0;
}

static int
fake_read_response(void *context, uint8_t *bytes, size_t capacity,
	size_t *actual_length)
{
	struct tpm2_crb_fake *fake = context;
	if (fake == NULL || bytes == NULL || actual_length == NULL ||
	    fake->fail_response || fake->response_length > capacity)
		return -1;
	memcpy(bytes, fake->response, fake->response_length);
	*actual_length = fake->reported_response_length == SIZE_MAX ?
	    fake->response_length : fake->reported_response_length;
	return 0;
}

const struct tpm2_crb_backend tpm2_crb_fake_backend = {
	.read32 = fake_read32,
	.write32 = fake_write32,
	.write_command = fake_write_command,
	.read_response = fake_read_response
};
