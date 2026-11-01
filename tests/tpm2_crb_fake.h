#ifndef TESTS_TPM2_CRB_FAKE_H
#define TESTS_TPM2_CRB_FAKE_H

#include "kernel/tpm2_crb.h"

#define TPM2_CRB_FAKE_BUFFER_SIZE 64U

struct tpm2_crb_fake {
	uint32_t registers[6];
	uint8_t command[TPM2_CRB_FAKE_BUFFER_SIZE];
	size_t command_length;
	uint8_t response[TPM2_CRB_FAKE_BUFFER_SIZE];
	size_t response_length;
	size_t reported_response_length;
	unsigned int locality_delay;
	unsigned int ready_delay;
	unsigned int idle_delay;
	unsigned int start_delay;
	int never_grant_locality;
	int never_ready;
	int never_idle;
	int never_complete;
	uint32_t fail_read_offset;
	uint32_t fail_write_offset;
	unsigned int read_count;
	unsigned int write_count;
	int saw_busy;
	int fail_read;
	int fail_write;
	int fail_command;
	int fail_response;
};

extern const struct tpm2_crb_backend tpm2_crb_fake_backend;
void tpm2_crb_fake_init(struct tpm2_crb_fake *fake);

#endif
