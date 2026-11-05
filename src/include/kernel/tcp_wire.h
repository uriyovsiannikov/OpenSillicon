#ifndef KERNEL_TCP_WIRE_H
#define KERNEL_TCP_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TCP_WIRE_HEADER_MIN 20U
#define TCP_WIRE_HEADER_MAX 60U
#define TCP_WIRE_OPTIONS_MAX 40U
/* IPv4 MTU 1500 minus its minimum 20-byte header. */
#define TCP_WIRE_SEGMENT_MAX 1480U

#define TCP_FLAG_FIN 0x01U
#define TCP_FLAG_SYN 0x02U
#define TCP_FLAG_RST 0x04U
#define TCP_FLAG_PSH 0x08U
#define TCP_FLAG_ACK 0x10U
#define TCP_FLAG_URG 0x20U
#define TCP_FLAG_ECE 0x40U
#define TCP_FLAG_CWR 0x80U

enum tcp_wire_error {
	TCP_WIRE_OK = 0,
	TCP_WIRE_INVALID_ARGUMENT,
	TCP_WIRE_LENGTH,
	TCP_WIRE_HEADER,
	TCP_WIRE_RESERVED,
	TCP_WIRE_OPTION,
	TCP_WIRE_CHECKSUM,
	TCP_WIRE_OUTPUT_SPACE
};

struct tcp_wire_pseudo_header {
	uint8_t source[4];
	uint8_t destination[4];
	uint8_t protocol;
	uint16_t segment_length;
};

struct tcp_wire_options {
	bool mss_present;
	uint16_t mss;
	bool window_scale_present;
	uint8_t window_scale;
	bool sack_permitted;
	uint8_t sack_block_count;
	bool timestamp_present;
	uint32_t timestamp_value;
	uint32_t timestamp_echo;
};

/* Options and payload point into caller-owned segment bytes. These borrowed
 * pointers remain valid only while that input buffer remains owned by caller.
 * Parsed option metadata is descriptive and does not negotiate extensions. */
struct tcp_wire_segment {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence;
	uint32_t acknowledgment;
	uint8_t data_offset;
	uint8_t reserved;
	uint8_t flags;
	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_pointer;
	const uint8_t *options;
	size_t options_length;
	struct tcp_wire_options parsed_options;
	const uint8_t *payload;
	size_t payload_length;
};

struct tcp_wire_spec {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence;
	uint32_t acknowledgment;
	uint8_t flags;
	uint16_t window;
	uint16_t urgent_pointer;
	const uint8_t *options;
	size_t options_length;
	const uint8_t *payload;
	size_t payload_length;
};

enum tcp_wire_error tcp_wire_parse(const struct tcp_wire_pseudo_header *pseudo,
	const uint8_t *segment, size_t available,
	struct tcp_wire_segment *parsed);
/* Serializes to caller-owned output; options and payload input ranges must not
 * overlap the output range. No allocation, transmission, or ownership transfer
 * occurs. */
enum tcp_wire_error tcp_wire_build(const uint8_t source[4],
	const uint8_t destination[4], const struct tcp_wire_spec *spec,
	uint8_t *output, size_t capacity, size_t *serialized_length);
/* Computes a checksum over exactly length bytes. To generate a segment
 * checksum, the checksum field in segment must be zero before this call. */
enum tcp_wire_error tcp_wire_checksum(const uint8_t source[4],
	const uint8_t destination[4], const uint8_t *segment, size_t length,
	uint16_t *checksum);

#endif
