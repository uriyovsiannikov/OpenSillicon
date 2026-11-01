#include "kernel/tcp_wire.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t source[4] = {192, 0, 2, 1};
static const uint8_t destination[4] = {198, 51, 100, 2};

static uint16_t
ref_checksum(const uint8_t *segment, size_t length)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < 4; i += 2) {
		sum += ((uint16_t)source[i] << 8) | source[i + 1];
		sum += ((uint16_t)destination[i] << 8) | destination[i + 1];
	}
	sum += 6U + (uint16_t)length;
	for (size_t i = 0; i < length; i += 2) {
		uint16_t word = (uint16_t)segment[i] << 8;
		if (i + 1 < length)
			word |= segment[i + 1];
		sum += word;
	}
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

static struct tcp_wire_pseudo_header
pseudo(size_t length)
{
	struct tcp_wire_pseudo_header p = {
		.source = {192, 0, 2, 1},
		.destination = {198, 51, 100, 2},
		.protocol = 6,
		.segment_length = (uint16_t)length
	};
	return p;
}

static void
test_independent_vectors_and_round_trip(void)
{
	/* Independently calculated network-order vectors:
	 * odd payload checksum = ae9a; MSS/NOP option SYN checksum = 4a4d. */
	static const uint8_t odd_fixture[] = {
		0x30,0x39,0x00,0x50,0x01,0x02,0x03,0x04,
		0x05,0x06,0x07,0x08,0x50,0x10,0x10,0x00,
		0xae,0x9a,0x00,0x00,0x61,0x62,0x63
	};
	static const uint8_t option_fixture[] = {
		0x30,0x39,0x00,0x50,0x01,0x02,0x03,0x04,
		0x05,0x06,0x07,0x08,0x70,0x02,0x10,0x00,
		0x4a,0x4d,0x00,0x00,0x01,0x01,0x02,0x04,
		0x05,0xb4,0x00,0x00
	};
	struct tcp_wire_pseudo_header p = pseudo(sizeof(odd_fixture));
	struct tcp_wire_segment parsed;
	uint8_t copy[sizeof(odd_fixture)];
	memcpy(copy, odd_fixture, sizeof(copy));
	assert(ref_checksum(odd_fixture, sizeof(odd_fixture)) == 0);
	copy[16] = copy[17] = 0;
	uint16_t generated_checksum = 0;
	assert(tcp_wire_checksum(source, destination, copy, sizeof(copy),
	    &generated_checksum) == TCP_WIRE_OK);
	assert(generated_checksum == 0xae9a);
	assert(tcp_wire_checksum(NULL, destination, copy, sizeof(copy),
	    &generated_checksum) == TCP_WIRE_INVALID_ARGUMENT);
	memcpy(copy, odd_fixture, sizeof(copy));
	assert(tcp_wire_parse(&p, odd_fixture, sizeof(odd_fixture), &parsed) ==
	    TCP_WIRE_OK);
	assert(parsed.source_port == 12345 && parsed.destination_port == 80);
	assert(parsed.sequence == 0x01020304 &&
	    parsed.acknowledgment == 0x05060708);
	assert(parsed.data_offset == 5 && parsed.reserved == 0);
	assert(parsed.flags == TCP_FLAG_ACK && parsed.window == 4096);
	assert(parsed.checksum == 0xae9a && parsed.urgent_pointer == 0);
	assert(parsed.options_length == 0 && parsed.payload_length == 3);
	assert(memcmp(parsed.payload, "abc", 3) == 0);
	assert(memcmp(copy, odd_fixture, sizeof(copy)) == 0);

	p = pseudo(sizeof(option_fixture));
	assert(ref_checksum(option_fixture, sizeof(option_fixture)) == 0);
	assert(tcp_wire_parse(&p, option_fixture, sizeof(option_fixture),
	    &parsed) == TCP_WIRE_OK);
	assert(parsed.flags == TCP_FLAG_SYN && parsed.data_offset == 7);
	assert(parsed.parsed_options.mss_present &&
	    parsed.parsed_options.mss == 1460);
	assert(parsed.options_length == 8 && parsed.payload_length == 0);

	uint8_t built[64];
	size_t built_length = 999;
	struct tcp_wire_spec spec = {
		.source_port = 12345, .destination_port = 80,
		.sequence = 0x01020304, .acknowledgment = 0x05060708,
		.flags = TCP_FLAG_ACK, .window = 4096,
		.payload = (const uint8_t *)"abc", .payload_length = 3
	};
	assert(tcp_wire_build(source, destination, &spec, built, sizeof(built),
	    &built_length) == TCP_WIRE_OK);
	assert(built_length == sizeof(odd_fixture));
	assert(memcmp(built, odd_fixture, sizeof(odd_fixture)) == 0);

	spec.flags = TCP_FLAG_SYN;
	spec.options = option_fixture + 20;
	spec.options_length = 8;
	spec.payload = NULL;
	spec.payload_length = 0;
	assert(tcp_wire_build(source, destination, &spec, built, sizeof(built),
	    &built_length) == TCP_WIRE_OK);
	assert(built_length == sizeof(option_fixture));
	assert(memcmp(built, option_fixture, sizeof(option_fixture)) == 0);

	spec.flags = TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_URG;
	spec.sequence = 0xfffffffeU;
	spec.acknowledgment = 0x01020305U;
	spec.urgent_pointer = 0x4321;
	spec.options = NULL;
	spec.options_length = 0;
	assert(tcp_wire_build(source, destination, &spec, built, sizeof(built),
	    &built_length) == TCP_WIRE_OK);
	p = pseudo(built_length);
	assert(tcp_wire_parse(&p, built, built_length, &parsed) == TCP_WIRE_OK);
	assert(parsed.flags == (TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_URG));
	assert(parsed.sequence == 0xfffffffeU &&
	    parsed.acknowledgment == 0x01020305U);
	assert(parsed.urgent_pointer == 0x4321);
	puts("TCP wire independent checksum vectors and builder/parser round trips PASS");
}

static size_t
make_option_segment(uint8_t *segment, const uint8_t *options,
	size_t option_length)
{
	size_t total = TCP_WIRE_HEADER_MIN + option_length;
	memset(segment, 0, total);
	segment[0] = 0x30;
	segment[1] = 0x39;
	segment[2] = 0;
	segment[3] = 80;
	segment[12] = (uint8_t)(total / 4U << 4);
	segment[13] = TCP_FLAG_SYN;
	memcpy(segment + TCP_WIRE_HEADER_MIN, options, option_length);
	uint16_t checksum = ref_checksum(segment, total);
	segment[16] = (uint8_t)(checksum >> 8);
	segment[17] = (uint8_t)checksum;
	return total;
}

static void
assert_option_invalid(const uint8_t *options, size_t options_length)
{
	uint8_t segment[80];
	size_t length = make_option_segment(segment, options, options_length);
	struct tcp_wire_pseudo_header p = pseudo(length);
	struct tcp_wire_segment parsed;
	enum tcp_wire_error error = tcp_wire_parse(&p, segment, length, &parsed);
	if (error != TCP_WIRE_OPTION)
		fprintf(stderr, "bad option test returned %u for kind=%u len=%u\n",
		    (unsigned)error, options[0], (unsigned)options_length);
	assert(error == TCP_WIRE_OPTION);
}

static void
test_option_validation(void)
{
	static const uint8_t zero_length[] = {2, 0, 0, 0};
	static const uint8_t one_length[] = {2, 1, 0, 0};
	static const uint8_t extends[] = {2, 8, 1, 1};
	static const uint8_t truncated_mss[] = {2, 5, 5, 0};
	static const uint8_t bad_scale[] = {3, 2, 0, 0};
	static const uint8_t bad_sack_permitted[] = {4, 3, 0, 0};
	static const uint8_t bad_sack_short[] = {5, 8, 0, 0};
	static const uint8_t bad_sack_alignment[] = {5, 11, 0, 0};
	static const uint8_t bad_timestamp[] = {8, 8, 0, 0};
	static const uint8_t eol_zero_padding[] = {1, 0, 0, 0};
	static const uint8_t eol_bad_padding[] = {0, 1, 0, 0};
	static const uint8_t sack_bad_alignment[] = {
		5,11,0,0,0,1,0,0,0,2,0,0
	};
	static const uint8_t unknown_well_formed[] = {30,4,0xaa,0xbb};
	assert_option_invalid(zero_length, sizeof(zero_length));
	assert_option_invalid(one_length, sizeof(one_length));
	assert_option_invalid(extends, sizeof(extends));
	assert_option_invalid(truncated_mss, sizeof(truncated_mss));
	assert_option_invalid(bad_scale, sizeof(bad_scale));
	assert_option_invalid(bad_sack_permitted, sizeof(bad_sack_permitted));
	assert_option_invalid(bad_sack_short, sizeof(bad_sack_short));
	assert_option_invalid(bad_sack_alignment, sizeof(bad_sack_alignment));
	assert_option_invalid(sack_bad_alignment, sizeof(sack_bad_alignment));
	assert_option_invalid(bad_timestamp, sizeof(bad_timestamp));
	uint8_t segment[80];
	size_t length = make_option_segment(segment, eol_zero_padding,
	    sizeof(eol_zero_padding));
	struct tcp_wire_pseudo_header p = pseudo(length);
	struct tcp_wire_segment parsed;
	assert(tcp_wire_parse(&p, segment, length, &parsed) == TCP_WIRE_OK);
	assert_option_invalid(eol_bad_padding, sizeof(eol_bad_padding));
	length = make_option_segment(segment, unknown_well_formed,
	    sizeof(unknown_well_formed));
	p = pseudo(length);
	assert(tcp_wire_parse(&p, segment, length, &parsed) == TCP_WIRE_OK);
	assert(parsed.options_length == sizeof(unknown_well_formed));

	/* Well-formed option metadata is exposed, but not negotiated or acted on. */
	static const uint8_t supported[] = {
		2,4,0x05,0xb4, 3,3,7, 4,2, 5,10,0,0,0,1,0,0,0,2,
		8,10,0,0,0,3,0,0,0,4, 0,0,0
	};
	length = make_option_segment(segment, supported, sizeof(supported));
	p = pseudo(length);
	assert(tcp_wire_parse(&p, segment, length, &parsed) == TCP_WIRE_OK);
	assert(parsed.parsed_options.mss_present &&
	    parsed.parsed_options.mss == 1460);
	assert(parsed.parsed_options.window_scale_present &&
	    parsed.parsed_options.window_scale == 7);
	assert(parsed.parsed_options.sack_permitted);
	assert(parsed.parsed_options.sack_block_count == 1);
	assert(parsed.parsed_options.timestamp_present &&
	    parsed.parsed_options.timestamp_value == 3 &&
	    parsed.parsed_options.timestamp_echo == 4);
	puts("TCP wire option validation and bounded metadata PASS");
}

static void
test_bounds_and_mutations(void)
{
	static const uint8_t base[] = {
		0x30,0x39,0x00,0x50,0x01,0x02,0x03,0x04,
		0x05,0x06,0x07,0x08,0x50,0x10,0x10,0x00,
		0xae,0x9a,0x00,0x00,0x61,0x62,0x63
	};
	uint8_t segment[TCP_WIRE_SEGMENT_MAX + 1];
	static uint8_t maximum_payload[TCP_WIRE_SEGMENT_MAX -
	    TCP_WIRE_HEADER_MIN];
	struct tcp_wire_segment parsed = {0};
	memcpy(segment, base, sizeof(base));
	for (size_t length = 0; length < TCP_WIRE_HEADER_MIN; length++) {
		struct tcp_wire_pseudo_header p = pseudo(length);
		assert(tcp_wire_parse(&p, segment, length, &parsed) != TCP_WIRE_OK);
	}
	struct tcp_wire_pseudo_header p = pseudo(sizeof(base));
	assert(tcp_wire_parse(NULL, base, sizeof(base), &parsed) ==
	    TCP_WIRE_INVALID_ARGUMENT);
	assert(tcp_wire_parse(&p, NULL, sizeof(base), &parsed) ==
	    TCP_WIRE_INVALID_ARGUMENT);
	assert(tcp_wire_parse(&p, base, sizeof(base), NULL) ==
	    TCP_WIRE_INVALID_ARGUMENT);
	assert(tcp_wire_parse(&p, base, sizeof(base) - 1, &parsed) ==
	    TCP_WIRE_LENGTH);
	/* Failed parses clear the entire public view, so borrowed pointers from a
	 * prior success cannot remain live after a rejected input. */
	assert(tcp_wire_parse(&(struct tcp_wire_pseudo_header){
	    .source = {192,0,2,1}, .destination = {198,51,100,2},
	    .protocol = 6, .segment_length = sizeof(base)
	    }, base, sizeof(base), &parsed) == TCP_WIRE_OK);
	assert(parsed.payload != NULL);
	assert(tcp_wire_parse(&p, base, sizeof(base) - 1, &parsed) ==
	    TCP_WIRE_LENGTH);
	{
		const struct tcp_wire_segment zero = {0};
		assert(memcmp(&parsed, &zero, sizeof(parsed)) == 0);
	}
	p.protocol = 17;
	assert(tcp_wire_parse(&p, base, sizeof(base), &parsed) == TCP_WIRE_LENGTH);
	p = pseudo(sizeof(base));
	segment[12] = 0x40;
	assert(tcp_wire_parse(&p, segment, sizeof(base), &parsed) ==
	    TCP_WIRE_HEADER);
	segment[12] = 0xf0;
	assert(tcp_wire_parse(&p, segment, sizeof(base), &parsed) ==
	    TCP_WIRE_HEADER);
	memcpy(segment, base, sizeof(base));
	segment[12] |= 1;
	segment[16] = segment[17] = 0;
	{
		uint16_t checksum = ref_checksum(segment, sizeof(base));
		segment[16] = (uint8_t)(checksum >> 8);
		segment[17] = (uint8_t)checksum;
	}
	assert(tcp_wire_parse(&p, segment, sizeof(base), &parsed) == TCP_WIRE_OK);
	assert(parsed.reserved == 1 && parsed.payload_length == 3);
	memcpy(segment, base, sizeof(base));
	segment[16] ^= 1;
	assert(tcp_wire_parse(&p, segment, sizeof(base), &parsed) ==
	    TCP_WIRE_CHECKSUM);
	memcpy(segment, base, sizeof(base));
	p = pseudo(sizeof(base));
	p.destination[3] ^= 1;
	assert(tcp_wire_parse(&p, segment, sizeof(base), &parsed) ==
	    TCP_WIRE_CHECKSUM);

	struct tcp_wire_spec spec = {
		.source_port = 1, .destination_port = 2,
		.flags = TCP_FLAG_ACK, .payload = maximum_payload,
		.payload_length = TCP_WIRE_SEGMENT_MAX - TCP_WIRE_HEADER_MIN
	};
	size_t output_length = 0;
	assert(tcp_wire_build(source, destination, &spec, segment,
	    sizeof(segment), &output_length) == TCP_WIRE_OK);
	assert(output_length == TCP_WIRE_SEGMENT_MAX);
	assert(tcp_wire_parse(&(struct tcp_wire_pseudo_header){
	    .source = {192,0,2,1}, .destination = {198,51,100,2},
	    .protocol = 6, .segment_length = TCP_WIRE_SEGMENT_MAX
	    }, segment, output_length, &parsed) == TCP_WIRE_OK);
	assert(tcp_wire_parse(&(struct tcp_wire_pseudo_header){
	    .source = {192,0,2,1}, .destination = {198,51,100,2},
	    .protocol = 6, .segment_length = TCP_WIRE_SEGMENT_MAX + 1U
	    }, segment, TCP_WIRE_SEGMENT_MAX + 1U, &parsed) == TCP_WIRE_LENGTH);
	spec.payload_length++;
	assert(tcp_wire_build(source, destination, &spec, segment,
	    sizeof(segment), &output_length) == TCP_WIRE_LENGTH);
	memcpy(segment, base, sizeof(base));
	for (size_t i = 0; i < sizeof(base); i++) {
		segment[i] ^= (uint8_t)(1U << (i % 8U));
		p = pseudo(sizeof(base));
		(void)tcp_wire_parse(&p, segment, sizeof(base), &parsed);
		segment[i] ^= (uint8_t)(1U << (i % 8U));
	}
	puts("TCP wire truncation, malformed fields, checksum, and size bounds PASS");
}

int
main(void)
{
	test_independent_vectors_and_round_trip();
	test_option_validation();
	test_bounds_and_mutations();
	puts("TCP wire tests passed; isolated serializer/parser only");
	return 0;
}
