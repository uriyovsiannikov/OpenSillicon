#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kernel/tpm2_crb.h"
#include "tpm2_crb_fake.h"

struct fixture {
	struct tpm2_crb_fake fake;
	struct tpm2_crb crb;
};

static void
fixture_init(struct fixture *fixture)
{
	tpm2_crb_fake_init(&fixture->fake);
	assert(tpm2_crb_init(&fixture->crb, &tpm2_crb_fake_backend,
	    &fixture->fake, TPM2_CRB_FAKE_BUFFER_SIZE,
	    TPM2_CRB_FAKE_BUFFER_SIZE) == TPM2_CRB_OK);
}

static void
reach_ready(struct fixture *fixture)
{
	assert(tpm2_crb_request_locality(&fixture->crb) == TPM2_CRB_OK);
	assert(fixture->crb.state == TPM2_CRB_STATE_LOCALITY);
	assert(tpm2_crb_command_ready(&fixture->crb) == TPM2_CRB_OK);
	assert(fixture->crb.state == TPM2_CRB_STATE_READY);
}

static void
test_initialization_and_registers(void)
{
	struct fixture fixture;
	uint32_t value = 0xdeadbeefU;
	fixture_init(&fixture);
	assert(fixture.crb.state == TPM2_CRB_STATE_IDLE);
	assert(tpm2_crb_read_register(&fixture.crb, TPM2_CRB_REG_LOC_STATE,
	    &value) == TPM2_CRB_OK);
	assert(value == TPM2_CRB_LOC_STATE_VALID);
	assert(tpm2_crb_read_register(&fixture.crb, 0x04, &value) ==
	    TPM2_CRB_INVALID_REGISTER);
	assert(tpm2_crb_write_register(&fixture.crb,
	    TPM2_CRB_REG_CTRL_STS, 0) == TPM2_CRB_INVALID_REGISTER);
	assert(tpm2_crb_write_register(&fixture.crb,
	    TPM2_CRB_REG_START, 2) == TPM2_CRB_INVALID_REGISTER);
	assert(tpm2_crb_write_register(&fixture.crb, TPM2_CRB_REG_LOC_CTRL,
	    TPM2_CRB_LOC_CTRL_REQUEST | TPM2_CRB_LOC_CTRL_RELEASE) ==
	    TPM2_CRB_INVALID_REGISTER);
	assert(tpm2_crb_write_register(&fixture.crb, TPM2_CRB_REG_CTRL_REQ,
	    TPM2_CRB_CTRL_CMD_READY | TPM2_CRB_CTRL_GO_IDLE) ==
	    TPM2_CRB_INVALID_REGISTER);
	assert(tpm2_crb_init(&fixture.crb, &tpm2_crb_fake_backend,
	    &fixture.fake, 0, 1) == TPM2_CRB_INVALID_ARGUMENT);
	assert(tpm2_crb_init(&fixture.crb, &tpm2_crb_fake_backend,
	    &fixture.fake, TPM2_CRB_MAX_BUFFER_SIZE + 1U, 1) ==
	    TPM2_CRB_INVALID_ARGUMENT);
}

static void
test_locality_ready_start_response_and_release(void)
{
	struct fixture fixture;
	const uint8_t command[] = {0x80, 0x01, 0, 0, 0, 4};
	const uint8_t expected[] = {0xaa, 0xbb, 0xcc};
	uint8_t response[8] = {0};
	size_t response_length = 99;
	fixture_init(&fixture);
	fixture.fake.locality_delay = 3;
	fixture.fake.ready_delay = 2;
	fixture.fake.start_delay = 4;
	memcpy(fixture.fake.response, expected, sizeof(expected));
	fixture.fake.response_length = sizeof(expected);
	assert(tpm2_crb_command_ready(&fixture.crb) == TPM2_CRB_INVALID_STATE);
	assert(tpm2_crb_request_locality(&fixture.crb) == TPM2_CRB_OK);
	assert(fixture.fake.registers[1] == TPM2_CRB_LOC_CTRL_REQUEST);
	assert(fixture.fake.registers[2] & TPM2_CRB_LOC_STS_GRANTED);
	assert(tpm2_crb_command_ready(&fixture.crb) == TPM2_CRB_OK);
	assert(fixture.fake.registers[3] == 0);
	assert(tpm2_crb_start(&fixture.crb, command, sizeof(command)) ==
	    TPM2_CRB_OK);
	assert(fixture.crb.state == TPM2_CRB_STATE_COMPLETE);
	assert(fixture.fake.saw_busy);
	assert(fixture.fake.command_length == sizeof(command));
	assert(memcmp(fixture.fake.command, command, sizeof(command)) == 0);
	assert(tpm2_crb_read_response(&fixture.crb, response, sizeof(response),
	    &response_length) == TPM2_CRB_OK);
	assert(response_length == sizeof(expected));
	assert(memcmp(response, expected, sizeof(expected)) == 0);
	assert(tpm2_crb_go_idle(&fixture.crb) == TPM2_CRB_OK);
	assert(fixture.crb.state == TPM2_CRB_STATE_LOCALITY);
	assert(tpm2_crb_release_locality(&fixture.crb) == TPM2_CRB_OK);
	assert(fixture.crb.state == TPM2_CRB_STATE_IDLE);
	assert((fixture.fake.registers[2] & TPM2_CRB_LOC_STS_GRANTED) == 0);
}

static void
test_buffer_bounds_and_invalid_transitions(void)
{
	struct fixture fixture;
	uint8_t command[TPM2_CRB_FAKE_BUFFER_SIZE + 1U] = {0};
	uint8_t response[TPM2_CRB_FAKE_BUFFER_SIZE] = {0};
	size_t actual = 77;
	fixture_init(&fixture);
	assert(tpm2_crb_start(&fixture.crb, command, 1) == TPM2_CRB_INVALID_STATE);
	reach_ready(&fixture);
	assert(tpm2_crb_start(&fixture.crb, NULL, 1) == TPM2_CRB_INVALID_LENGTH);
	assert(tpm2_crb_start(&fixture.crb, command, 0) == TPM2_CRB_INVALID_LENGTH);
	assert(tpm2_crb_start(&fixture.crb, command, sizeof(command)) ==
	    TPM2_CRB_INVALID_LENGTH);
	assert(tpm2_crb_read_response(&fixture.crb, response, sizeof(response),
	    &actual) == TPM2_CRB_INVALID_STATE);
	assert(tpm2_crb_go_idle(&fixture.crb) == TPM2_CRB_OK);
	assert(tpm2_crb_release_locality(&fixture.crb) == TPM2_CRB_OK);
	assert(tpm2_crb_release_locality(&fixture.crb) == TPM2_CRB_INVALID_STATE);
}

static void
test_timeout_is_bounded_and_faults_closed(void)
{
	struct fixture fixture;
	fixture_init(&fixture);
	fixture.fake.never_grant_locality = 1;
	assert(tpm2_crb_request_locality(&fixture.crb) == TPM2_CRB_TIMEOUT);
	assert(fixture.fake.read_count == TPM2_CRB_MAX_POLLS * 2U);
	assert(fixture.crb.state == TPM2_CRB_STATE_FAULT);
	assert(tpm2_crb_request_locality(&fixture.crb) == TPM2_CRB_INVALID_STATE);

	fixture_init(&fixture);
	reach_ready(&fixture);
	fixture.fake.never_complete = 1;
	{
		const uint8_t command = 1;
		assert(tpm2_crb_start(&fixture.crb, &command, 1) ==
		    TPM2_CRB_TIMEOUT);
	}
	assert(fixture.crb.state == TPM2_CRB_STATE_FAULT);
	assert(fixture.fake.read_count <= TPM2_CRB_MAX_POLLS * 3U + 3U);
}

static void
test_backend_errors_and_device_status(void)
{
	struct fixture fixture;
	uint32_t value = 0;
	fixture_init(&fixture);
	fixture.fake.fail_read = 1;
	fixture.fake.fail_read_offset = TPM2_CRB_REG_LOC_STATE;
	assert(tpm2_crb_read_register(&fixture.crb, TPM2_CRB_REG_LOC_STATE,
	    &value) == TPM2_CRB_BACKEND_ERROR);
	fixture.fake.fail_read = 0;
	fixture.fake.fail_write = 1;
	fixture.fake.fail_write_offset = TPM2_CRB_REG_LOC_CTRL;
	assert(tpm2_crb_request_locality(&fixture.crb) == TPM2_CRB_BACKEND_ERROR);
	assert(fixture.crb.state == TPM2_CRB_STATE_FAULT);

	fixture_init(&fixture);
	reach_ready(&fixture);
	fixture.fake.registers[4] |= TPM2_CRB_CTRL_STS_ERROR;
	{
		const uint8_t command = 1;
		assert(tpm2_crb_start(&fixture.crb, &command, 1) ==
		    TPM2_CRB_DEVICE_ERROR);
	}
	assert(fixture.crb.state == TPM2_CRB_STATE_FAULT);
}

static void
test_response_bounds_and_backend_failure(void)
{
	struct fixture fixture;
	uint8_t response[8];
	size_t actual = 123;
	const uint8_t command = 1;
	fixture_init(&fixture);
	reach_ready(&fixture);
	assert(tpm2_crb_start(&fixture.crb, &command, 1) == TPM2_CRB_OK);
	assert(tpm2_crb_read_response(&fixture.crb, response, 0, &actual) ==
	    TPM2_CRB_INVALID_LENGTH);
	assert(tpm2_crb_read_response(&fixture.crb, response,
	    TPM2_CRB_FAKE_BUFFER_SIZE + 1U, &actual) == TPM2_CRB_INVALID_LENGTH);
	fixture.fake.reported_response_length = sizeof(response) + 1U;
	memset(response, 0xa5, sizeof(response));
	assert(tpm2_crb_read_response(&fixture.crb, response, sizeof(response),
	    &actual) == TPM2_CRB_INVALID_LENGTH);
	for (size_t i = 0; i < sizeof(response); i++)
		assert(response[i] == 0);
	assert(actual == 123);

	fixture_init(&fixture);
	reach_ready(&fixture);
	assert(tpm2_crb_start(&fixture.crb, &command, 1) == TPM2_CRB_OK);
	fixture.fake.fail_response = 1;
	memset(response, 0xa5, sizeof(response));
	assert(tpm2_crb_read_response(&fixture.crb, response, sizeof(response),
	    &actual) == TPM2_CRB_BACKEND_ERROR);
	assert(actual == 123);
	for (size_t i = 0; i < sizeof(response); i++)
		assert(response[i] == 0xa5);
}

static void
test_command_backend_error_and_repeated_cycle(void)
{
	struct fixture fixture;
	const uint8_t command[] = {1, 2, 3, 4};
	fixture_init(&fixture);
	reach_ready(&fixture);
	fixture.fake.fail_command = 1;
	assert(tpm2_crb_start(&fixture.crb, command, sizeof(command)) ==
	    TPM2_CRB_BACKEND_ERROR);
	assert(fixture.crb.state == TPM2_CRB_STATE_FAULT);

	fixture_init(&fixture);
	for (unsigned int i = 0; i < 4; i++) {
		reach_ready(&fixture);
		assert(tpm2_crb_start(&fixture.crb, command, sizeof(command)) ==
		    TPM2_CRB_OK);
		assert(tpm2_crb_go_idle(&fixture.crb) == TPM2_CRB_OK);
		assert(tpm2_crb_release_locality(&fixture.crb) == TPM2_CRB_OK);
	}
}

int
main(void)
{
	test_initialization_and_registers();
	test_locality_ready_start_response_and_release();
	test_buffer_bounds_and_invalid_transitions();
	test_timeout_is_bounded_and_faults_closed();
	test_backend_errors_and_device_status();
	test_response_bounds_and_backend_failure();
	test_command_backend_error_and_repeated_cycle();
	puts("TPM2 CRB abstraction/fake transport tests passed");
	return 0;
}
