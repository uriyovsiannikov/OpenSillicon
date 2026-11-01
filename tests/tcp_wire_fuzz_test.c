#include "kernel/tcp_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FUZZ_SEED 0x15c5f00dU
#define FUZZ_CASES 100000U
#define GUARD_SIZE 16U
#define FUZZ_BUFFER_SIZE (TCP_WIRE_SEGMENT_MAX + 32U)

static const uint8_t source[4] = {10, 15, 3, 1};
static const uint8_t destination[4] = {10, 15, 3, 2};
static uint32_t random_state = FUZZ_SEED;

static uint32_t
next_random(void)
{
	uint32_t x = random_state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	random_state = x;
	return x;
}

static void
fill_random(uint8_t *bytes, size_t length)
{
	for (size_t i = 0; i < length; i++)
		bytes[i] = (uint8_t)next_random();
}

struct guarded_result {
	uint8_t before[GUARD_SIZE];
	struct tcp_wire_segment segment;
	uint8_t after[GUARD_SIZE];
};

static bool
all_byte(const uint8_t *bytes, size_t length, uint8_t value)
{
	for (size_t i = 0; i < length; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static void
exercise(const uint8_t *bytes, size_t available, uint8_t protocol,
	uint16_t pseudo_length)
{
	struct tcp_wire_pseudo_header pseudo = {0};
	struct guarded_result output;
	uint8_t input_copy[FUZZ_BUFFER_SIZE];
	enum tcp_wire_error error;
	memcpy(input_copy, bytes, available);
	memcpy(pseudo.source, source, sizeof(source));
	memcpy(pseudo.destination, destination, sizeof(destination));
	pseudo.protocol = protocol;
	pseudo.segment_length = pseudo_length;
	memset(&output, 0xa5, sizeof(output));
	error = tcp_wire_parse(&pseudo, bytes, available, &output.segment);
	assert(memcmp(bytes, input_copy, available) == 0);
	assert(all_byte(output.before, sizeof(output.before), 0xa5));
	assert(all_byte(output.after, sizeof(output.after), 0xa5));
	if (error != TCP_WIRE_OK) {
		const struct tcp_wire_segment zero = {0};
		assert(memcmp(&output.segment, &zero, sizeof(zero)) == 0);
		return;
	}
	assert(protocol == 6 && pseudo_length == available);
	assert(available >= TCP_WIRE_HEADER_MIN &&
	    available <= TCP_WIRE_SEGMENT_MAX);
	assert(output.segment.source_port != 0 &&
	    output.segment.destination_port != 0);
	assert(output.segment.reserved == 0);
	assert(output.segment.data_offset >= 5 &&
	    output.segment.data_offset <= 15);
	assert((size_t)output.segment.data_offset * 4U <= available);
	assert(output.segment.options_length ==
	    (size_t)output.segment.data_offset * 4U - TCP_WIRE_HEADER_MIN);
	assert(output.segment.payload_length == available -
	    (size_t)output.segment.data_offset * 4U);
	assert(output.segment.options == bytes + TCP_WIRE_HEADER_MIN);
	assert(output.segment.payload == bytes +
	    (size_t)output.segment.data_offset * 4U);
}

static void
test_valid_fixture_mutations(void)
{
	static const uint8_t options[] = {2, 4, 0x05, 0xb4};
	static const uint8_t payload[] = "wire-fuzz-boundary";
	uint8_t fixture[TCP_WIRE_SEGMENT_MAX];
	struct tcp_wire_spec spec = {
		.source_port = 41000, .destination_port = 443,
		.sequence = 0xfffffffeU, .acknowledgment = 0x10203040U,
		.flags = TCP_FLAG_SYN, .window = 4096,
		.options = options, .options_length = sizeof(options),
		.payload = payload, .payload_length = sizeof(payload) - 1U
	};
	size_t length = 0;
	assert(tcp_wire_build(source, destination, &spec, fixture,
	    sizeof(fixture), &length) == TCP_WIRE_OK);
	exercise(fixture, length, 6, (uint16_t)length);
	for (size_t truncated = 0; truncated < length; truncated++)
		exercise(fixture, truncated, 6, (uint16_t)truncated);
	for (unsigned offset = 0; offset < 16; offset++) {
		uint8_t mutation[TCP_WIRE_SEGMENT_MAX];
		memcpy(mutation, fixture, length);
		mutation[12] = (uint8_t)(offset << 4);
		exercise(mutation, length, 6, (uint16_t)length);
	}
	for (size_t byte = 16; byte < 18; byte++) {
		for (unsigned bit = 0; bit < 8; bit++) {
			uint8_t mutation[TCP_WIRE_SEGMENT_MAX];
			memcpy(mutation, fixture, length);
			mutation[byte] ^= (uint8_t)(1U << bit);
			exercise(mutation, length, 6, (uint16_t)length);
		}
	}
	{
		uint8_t mutation[TCP_WIRE_SEGMENT_MAX];
		memcpy(mutation, fixture, length);
		mutation[20] = 2;
		mutation[21] = 0;
		/* Restore a valid checksum so the malformed option path is reached. */
		mutation[16] = mutation[17] = 0;
		uint16_t checksum = 0;
		assert(tcp_wire_checksum(source, destination, mutation, length,
		    &checksum) == TCP_WIRE_OK);
		mutation[16] = (uint8_t)(checksum >> 8);
		mutation[17] = (uint8_t)checksum;
		exercise(mutation, length, 6, (uint16_t)length);
	}
	exercise(fixture, length, 17, (uint16_t)length);
	exercise(fixture, length, 6, (uint16_t)(length - 1U));
}

static void
test_systematic_boundaries(void)
{
	uint8_t guarded[FUZZ_BUFFER_SIZE + 2U * GUARD_SIZE];
	uint8_t *bytes = guarded + GUARD_SIZE;
	memset(guarded, 0x6d, sizeof(guarded));
	fill_random(bytes, FUZZ_BUFFER_SIZE);
	for (size_t length = 0; length <= FUZZ_BUFFER_SIZE; length++)
		exercise(bytes, length, 6, (uint16_t)length);
	for (unsigned offset = 0; offset <= 15; offset++) {
		bytes[12] = (uint8_t)(offset << 4);
		exercise(bytes, TCP_WIRE_HEADER_MIN, 6, TCP_WIRE_HEADER_MIN);
	}
	for (unsigned flags = 0; flags <= 255; flags++) {
		bytes[12] = 0x50;
		bytes[13] = (uint8_t)flags;
		exercise(bytes, TCP_WIRE_HEADER_MIN, 6, TCP_WIRE_HEADER_MIN);
	}
	assert(all_byte(guarded, GUARD_SIZE, 0x6d));
	assert(all_byte(bytes + FUZZ_BUFFER_SIZE, GUARD_SIZE, 0x6d));
}

static void
test_seeded_generated_inputs(void)
{
	uint8_t guarded[FUZZ_BUFFER_SIZE + 2U * GUARD_SIZE];
	uint8_t *bytes = guarded + GUARD_SIZE;
	memset(guarded, 0xc3, sizeof(guarded));
	for (size_t i = 0; i < FUZZ_CASES; i++) {
		size_t available = next_random() % (FUZZ_BUFFER_SIZE + 1U);
		uint8_t protocol = (uint8_t)next_random();
		uint16_t pseudo_length = (uint16_t)next_random();
		fill_random(bytes, FUZZ_BUFFER_SIZE);
		/* Bias some inputs toward realistic header/option boundaries and
		 * valid lengths while retaining fully arbitrary generated packets. */
		if ((i & 3U) == 0 && available >= TCP_WIRE_HEADER_MIN)
			bytes[12] = (uint8_t)((next_random() % 16U) << 4) |
			    (uint8_t)(next_random() & 0x0fU);
		if ((i & 7U) == 0) {
			protocol = 6;
			pseudo_length = (uint16_t)available;
		}
		if ((i & 15U) == 0 && available >= TCP_WIRE_HEADER_MIN)
			bytes[16] ^= 1U;
		exercise(bytes, available, protocol, pseudo_length);
	}
	assert(all_byte(guarded, GUARD_SIZE, 0xc3));
	assert(all_byte(bytes + FUZZ_BUFFER_SIZE, GUARD_SIZE, 0xc3));
}

int
main(void)
{
	test_systematic_boundaries();
	test_valid_fixture_mutations();
	test_seeded_generated_inputs();
	printf("TCP wire deterministic parser fuzz PASS: seed=0x%08x generated=%u plus systematic boundaries\n",
	    FUZZ_SEED, FUZZ_CASES);
	return 0;
}
