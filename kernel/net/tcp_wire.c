#include "kernel/tcp_wire.h"

#define TCP_OPTION_EOL 0U
#define TCP_OPTION_NOP 1U
#define TCP_OPTION_MSS 2U
#define TCP_OPTION_WINDOW_SCALE 3U
#define TCP_OPTION_SACK_PERMITTED 4U
#define TCP_OPTION_SACK 5U
#define TCP_OPTION_TIMESTAMP 8U

static uint16_t
read_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t
read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
}

static void
write_be16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static void
write_be32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static uint32_t
sum_bytes(uint32_t sum, const uint8_t *data, size_t length)
{
	while (length >= 2) {
		sum += read_be16(data);
		data += 2;
		length -= 2;
	}
	if (length != 0)
		sum += (uint16_t)data[0] << 8;
	return sum;
}

static uint32_t
pseudo_sum(const uint8_t source[4], const uint8_t destination[4],
	uint8_t protocol, uint16_t length)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < 4; i += 2) {
		sum += read_be16(source + i);
		sum += read_be16(destination + i);
	}
	sum += protocol;
	sum += length;
	return sum;
}

static uint16_t
finish_checksum(uint32_t sum)
{
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

enum tcp_wire_error
tcp_wire_checksum(const uint8_t source[4], const uint8_t destination[4],
	const uint8_t *segment, size_t length, uint16_t *checksum)
{
	if (source == NULL || destination == NULL || segment == NULL ||
	    checksum == NULL)
		return TCP_WIRE_INVALID_ARGUMENT;
	if (length < TCP_WIRE_HEADER_MIN || length > TCP_WIRE_SEGMENT_MAX)
		return TCP_WIRE_LENGTH;
	*checksum = finish_checksum(sum_bytes(pseudo_sum(source, destination, 6,
	    (uint16_t)length), segment, length));
	return TCP_WIRE_OK;
}

static enum tcp_wire_error
parse_options(const uint8_t *options, size_t length, uint8_t flags,
	struct tcp_wire_options *parsed)
{
	size_t offset = 0;
	while (offset < length) {
		uint8_t kind = options[offset];
		if (kind == TCP_OPTION_EOL) {
			for (size_t i = offset + 1; i < length; i++)
				if (options[i] != 0)
					return TCP_WIRE_OPTION;
			return TCP_WIRE_OK;
		}
		if (kind == TCP_OPTION_NOP) {
			offset++;
			continue;
		}
		if (length - offset < 2)
			return TCP_WIRE_OPTION;
		uint8_t option_length = options[offset + 1];
		if (option_length < 2 || option_length > length - offset)
			return TCP_WIRE_OPTION;
		switch (kind) {
		case TCP_OPTION_MSS:
			if (option_length != 4 || parsed->mss_present ||
			    (flags & TCP_FLAG_SYN) == 0 ||
			    read_be16(options + offset + 2) == 0)
				return TCP_WIRE_OPTION;
			parsed->mss_present = true;
			parsed->mss = read_be16(options + offset + 2);
			break;
		case TCP_OPTION_WINDOW_SCALE:
			if (option_length != 3 || parsed->window_scale_present ||
			    (flags & TCP_FLAG_SYN) == 0 || options[offset + 2] > 14)
				return TCP_WIRE_OPTION;
			parsed->window_scale_present = true;
			parsed->window_scale = options[offset + 2];
			break;
		case TCP_OPTION_SACK_PERMITTED:
			if (option_length != 2 || parsed->sack_permitted ||
			    (flags & TCP_FLAG_SYN) == 0)
				return TCP_WIRE_OPTION;
			parsed->sack_permitted = true;
			break;
		case TCP_OPTION_SACK:
			if (parsed->sack_block_count != 0 || option_length < 10 ||
			    ((option_length - 2U) % 8U) != 0)
				return TCP_WIRE_OPTION;
			parsed->sack_block_count = (uint8_t)((option_length - 2U) / 8U);
			break;
		case TCP_OPTION_TIMESTAMP:
			if (option_length != 10 || parsed->timestamp_present)
				return TCP_WIRE_OPTION;
			parsed->timestamp_present = true;
			parsed->timestamp_value = read_be32(options + offset + 2);
			parsed->timestamp_echo = read_be32(options + offset + 6);
			break;
		default:
			/* Unknown well-formed options are retained as opaque bytes. */
			break;
		}
		offset += option_length;
	}
	return TCP_WIRE_OK;
}

enum tcp_wire_error
tcp_wire_parse(const struct tcp_wire_pseudo_header *pseudo,
	const uint8_t *segment, size_t available,
	struct tcp_wire_segment *parsed)
{
	struct tcp_wire_segment result = {0};
	struct tcp_wire_options options = {0};
	size_t header_length;
	enum tcp_wire_error error;
	if (pseudo == NULL || segment == NULL || parsed == NULL)
		return TCP_WIRE_INVALID_ARGUMENT;
	if (pseudo->protocol != 6 || pseudo->segment_length != available ||
	    available < TCP_WIRE_HEADER_MIN || available > TCP_WIRE_SEGMENT_MAX)
		return TCP_WIRE_LENGTH;
	header_length = (size_t)(segment[12] >> 4) * 4U;
	if (header_length < TCP_WIRE_HEADER_MIN ||
	    header_length > TCP_WIRE_HEADER_MAX || header_length > available)
		return TCP_WIRE_HEADER;
	result.reserved = (uint8_t)(segment[12] & 0x0fU);
	if (result.reserved != 0)
		return TCP_WIRE_RESERVED;
	result.source_port = read_be16(segment);
	result.destination_port = read_be16(segment + 2);
	if (result.source_port == 0 || result.destination_port == 0)
		return TCP_WIRE_HEADER;
	result.sequence = read_be32(segment + 4);
	result.acknowledgment = read_be32(segment + 8);
	result.data_offset = (uint8_t)(header_length / 4U);
	result.flags = segment[13];
	result.window = read_be16(segment + 14);
	result.checksum = read_be16(segment + 16);
	result.urgent_pointer = read_be16(segment + 18);
	result.options = segment + TCP_WIRE_HEADER_MIN;
	result.options_length = header_length - TCP_WIRE_HEADER_MIN;
	error = parse_options(result.options, result.options_length,
	    result.flags, &options);
	if (error != TCP_WIRE_OK)
		return error;
	if (finish_checksum(sum_bytes(pseudo_sum(pseudo->source,
	    pseudo->destination, pseudo->protocol, pseudo->segment_length),
	    segment, available)) != 0)
		return TCP_WIRE_CHECKSUM;
	result.parsed_options = options;
	result.payload = segment + header_length;
	result.payload_length = available - header_length;
	*parsed = result;
	return TCP_WIRE_OK;
}

enum tcp_wire_error
tcp_wire_build(const uint8_t source[4], const uint8_t destination[4],
	const struct tcp_wire_spec *spec, uint8_t *output, size_t capacity,
	size_t *serialized_length)
{
	struct tcp_wire_options parsed_options = {0};
	size_t header_length;
	size_t total_length;
	if (source == NULL || destination == NULL || spec == NULL ||
	    output == NULL || serialized_length == NULL ||
	    spec->source_port == 0 || spec->destination_port == 0 ||
	    (spec->options_length != 0 && spec->options == NULL) ||
	    (spec->payload_length != 0 && spec->payload == NULL) ||
	    spec->options_length > TCP_WIRE_OPTIONS_MAX ||
	    (spec->options_length & 3U) != 0)
		return TCP_WIRE_INVALID_ARGUMENT;
	if (parse_options(spec->options, spec->options_length, spec->flags,
	    &parsed_options) != TCP_WIRE_OK)
		return TCP_WIRE_OPTION;
	header_length = TCP_WIRE_HEADER_MIN + spec->options_length;
	if (spec->payload_length > TCP_WIRE_SEGMENT_MAX - header_length)
		return TCP_WIRE_LENGTH;
	total_length = header_length + spec->payload_length;
	if (capacity < total_length)
		return TCP_WIRE_OUTPUT_SPACE;
	for (size_t i = 0; i < total_length; i++)
		output[i] = 0;
	write_be16(output, spec->source_port);
	write_be16(output + 2, spec->destination_port);
	write_be32(output + 4, spec->sequence);
	write_be32(output + 8, spec->acknowledgment);
	output[12] = (uint8_t)((header_length / 4U) << 4);
	output[13] = spec->flags;
	write_be16(output + 14, spec->window);
	write_be16(output + 18, spec->urgent_pointer);
	for (size_t i = 0; i < spec->options_length; i++)
		output[TCP_WIRE_HEADER_MIN + i] = spec->options[i];
	for (size_t i = 0; i < spec->payload_length; i++)
		output[header_length + i] = spec->payload[i];
	uint16_t checksum;
	if (tcp_wire_checksum(source, destination, output, total_length,
	    &checksum) != TCP_WIRE_OK)
		return TCP_WIRE_LENGTH;
	write_be16(output + 16, checksum);
	*serialized_length = total_length;
	return TCP_WIRE_OK;
}
