#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/netstack.h"
#include "kernel/tcp_wire.h"

static size_t tcp_wire_parse_calls;

enum tcp_wire_error __real_tcp_wire_parse(
	const struct tcp_wire_pseudo_header *pseudo, const uint8_t *segment,
	size_t available, struct tcp_wire_segment *parsed);

enum tcp_wire_error
__wrap_tcp_wire_parse(const struct tcp_wire_pseudo_header *pseudo,
	const uint8_t *segment, size_t available,
	struct tcp_wire_segment *parsed)
{
	tcp_wire_parse_calls++;
	return __real_tcp_wire_parse(pseudo, segment, available, parsed);
}

static const struct netstack_config config = {
	.mac = {0x52, 0x54, 0x00, 0x15, 0x03, 0x01},
	.address = {10, 15, 3, 1},
	.netmask = {255, 255, 255, 0}
};

static void
be16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static uint16_t
sum(const uint8_t *p, size_t length)
{
	uint32_t value = 0;
	while (length >= 2) {
		value += ((uint16_t)p[0] << 8) | p[1];
		p += 2;
		length -= 2;
	}
	if (length != 0)
		value += (uint16_t)p[0] << 8;
	while (value >> 16)
		value = (value & 0xffffU) + (value >> 16);
	return (uint16_t)~value;
}

static size_t
arp_frame(uint8_t *frame, uint16_t operation)
{
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	for (size_t i = 0; i < 6; i++) {
		frame[i] = operation == 1 ? 0xff : config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	be16(frame + 12, 0x0806);
	be16(frame + 14, 1);
	be16(frame + 16, 0x0800);
	frame[18] = 6;
	frame[19] = 4;
	be16(frame + 20, operation);
	for (size_t i = 0; i < 6; i++)
		frame[22 + i] = peer_mac[i];
	frame[28] = 10; frame[29] = 15; frame[30] = 3; frame[31] = 2;
	for (size_t i = 0; i < 6; i++)
		frame[32 + i] = operation == 1 ? 0 : config.mac[i];
	frame[38] = 10; frame[39] = 15; frame[40] = 3; frame[41] = 1;
	return 42;
}

static size_t
ipv4_echo(uint8_t *frame, uint8_t type, const uint8_t *payload,
	size_t payload_length)
{
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	uint8_t *ip = frame + 14;
	uint8_t *icmp = ip + 20;
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	be16(frame + 12, 0x0800);
	for (size_t i = 0; i < 20; i++) ip[i] = 0;
	ip[0] = 0x45;
	be16(ip + 2, (uint16_t)(28 + payload_length));
	be16(ip + 4, 0x153a);
	ip[8] = 64;
	ip[9] = 1;
	ip[12] = 10; ip[13] = 15; ip[14] = 3; ip[15] = 2;
	ip[16] = 10; ip[17] = 15; ip[18] = 3; ip[19] = 1;
	be16(ip + 10, sum(ip, 20));
	icmp[0] = type; icmp[1] = 0; be16(icmp + 2, 0);
	be16(icmp + 4, 0x1234); be16(icmp + 6, 7);
	for (size_t i = 0; i < payload_length; i++) icmp[8 + i] = payload[i];
	be16(icmp + 2, sum(icmp, 8 + payload_length));
	return 14 + 28 + payload_length;
}

static size_t
ipv4_dest_unreachable(uint8_t *frame, const uint8_t *quoted_ip,
	size_t quoted_length)
{
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	uint8_t *ip = frame + 14;
	uint8_t *icmp = ip + 20;
	size_t icmp_length = 8 + quoted_length;
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	be16(frame + 12, 0x0800);
	for (size_t i = 0; i < 20 + icmp_length; i++)
		ip[i] = 0;
	ip[0] = 0x45;
	be16(ip + 2, (uint16_t)(20 + icmp_length));
	ip[8] = 64;
	ip[9] = 1;
	ip[12] = 10; ip[13] = 15; ip[14] = 3; ip[15] = 2;
	for (size_t i = 0; i < 4; i++)
		ip[16 + i] = config.address[i];
	be16(ip + 10, sum(ip, 20));
	icmp[0] = 3; icmp[1] = 1;
	for (size_t i = 0; i < quoted_length; i++)
		icmp[8 + i] = quoted_ip[i];
	be16(icmp + 2, sum(icmp, icmp_length));
	return 14 + 20 + icmp_length;
}

static size_t
ipv4_protocol_frame(uint8_t *frame, uint8_t protocol, size_t payload_length)
{
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	uint8_t *ip = frame + NETSTACK_ETHERNET_HEADER;
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	be16(frame + 12, 0x0800);
	for (size_t i = 0; i < NETSTACK_IPV4_HEADER + payload_length; i++)
		ip[i] = 0;
	ip[0] = 0x45;
	be16(ip + 2, (uint16_t)(NETSTACK_IPV4_HEADER + payload_length));
	ip[8] = 64;
	ip[9] = protocol;
	ip[12] = 10; ip[13] = 15; ip[14] = 3; ip[15] = 2;
	for (size_t i = 0; i < 4; i++)
		ip[16 + i] = config.address[i];
	be16(ip + 10, sum(ip, NETSTACK_IPV4_HEADER));
	return NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + payload_length;
}

static size_t
tcp_frame(uint8_t *frame, uint8_t flags, const uint8_t *options,
	size_t options_length, const uint8_t *payload, size_t payload_length)
{
	static const uint8_t peer_ip[4] = {10, 15, 3, 2};
	struct tcp_wire_spec spec = {
		.source_port = 443,
		.destination_port = 42000,
		.sequence = 7,
		.flags = flags,
		.window = 4096,
		.options = options,
		.options_length = options_length,
		.payload = payload,
		.payload_length = payload_length
	};
	size_t tcp_length;
	size_t expected_length = TCP_WIRE_HEADER_MIN + options_length +
	    payload_length;
	size_t frame_length = ipv4_protocol_frame(frame, 6, expected_length);
	assert(tcp_wire_build(peer_ip, config.address, &spec,
	    frame + NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER,
	    expected_length, &tcp_length) == TCP_WIRE_OK);
	assert(tcp_length == expected_length);
	return frame_length;
}

static void
repair_tcp_checksum(uint8_t *frame, size_t tcp_length)
{
	static const uint8_t peer_ip[4] = {10, 15, 3, 2};
	uint8_t *tcp = frame + NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER;
	uint16_t checksum;
	be16(tcp + 16, 0);
	assert(tcp_wire_checksum(peer_ip, config.address, tcp, tcp_length,
	    &checksum) == TCP_WIRE_OK);
	be16(tcp + 16, checksum);
}

static uint16_t
udp_sum(const uint8_t source[4], const uint8_t destination[4],
	const uint8_t *udp, size_t length)
{
	uint32_t value = 0;
	for (size_t i = 0; i < 4; i += 2) {
		value += ((uint16_t)source[i] << 8) | source[i + 1];
		value += ((uint16_t)destination[i] << 8) | destination[i + 1];
	}
	value += 17U + (uint16_t)length;
	value += (uint32_t)(~sum(udp, length) & 0xffffU);
	while (value >> 16)
		value = (value & 0xffffU) + (value >> 16);
	return (uint16_t)~value;
}

static void
test_udp(void)
{
	struct netstack stack;
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	static const uint8_t peer_ip[4] = {10, 15, 3, 2};
	static const uint8_t request[] = "udp-checksum";
	uint8_t frame[NETSTACK_MAX_FRAME] = {0};
	uint8_t reply[NETSTACK_MAX_FRAME];
	struct netstack_udp_view view;
	size_t length, reply_length;
	uint8_t *ip, *udp;
	assert(netstack_init(&stack, &config));
	assert(netstack_build_udp_frame(&stack, peer_mac, peer_ip, 45000, 45001,
	    request, sizeof(request) - 1, frame, sizeof(frame), &length));
	assert(length == 14 + 20 + 8 + sizeof(request) - 1);
	assert(frame[12] == 0x08 && frame[13] == 0x00);
	/* Turn the correctly built datagram into a peer-to-guest packet. */
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	ip = frame + 14;
	for (size_t i = 0; i < 4; i++) {
		ip[12 + i] = peer_ip[i];
		ip[16 + i] = config.address[i];
	}
	be16(ip + 10, 0);
	be16(ip + 10, sum(ip, 20));
	udp = ip + 20;
	be16(udp, 45001);
	be16(udp + 2, 45000);
	be16(udp + 6, 0);
	be16(udp + 6, udp_sum(peer_ip, config.address, udp,
	    length - 14 - 20));
	{
		enum netstack_ipv4_protocol protocol;
		assert(netstack_ipv4_classify(ip, length - 14, &protocol));
		assert(protocol == NETSTACK_IPV4_UDP);
	}
	assert(netstack_process_frame(&stack, frame, length, 2, reply,
	    sizeof(reply), &reply_length, &view) == NETSTACK_UDP_DATAGRAM);
	assert(reply_length == 0 && view.source_port == 45001 &&
	    view.destination_port == 45000 &&
	    view.payload_length == sizeof(request) - 1);
	for (size_t i = 0; i < view.payload_length; i++)
		assert(view.payload[i] == request[i]);
	/* IPv4 permits an omitted UDP checksum. */
	be16(udp + 6, 0);
	assert(netstack_process_frame(&stack, frame, length, 3, reply,
	    sizeof(reply), &reply_length, &view) == NETSTACK_UDP_DATAGRAM);
	/* A present but incorrect checksum and malformed lengths are rejected. */
	be16(udp + 6, udp_sum(peer_ip, config.address, udp,
	    length - 14 - 20));
	udp[8] ^= 0x80;
	assert(netstack_process_frame(&stack, frame, length, 4, reply,
	    sizeof(reply), &reply_length, &view) == NETSTACK_IGNORED);
	udp[8] ^= 0x80;
	be16(udp + 6, 1);
	assert(netstack_process_frame(&stack, frame, length, 5, reply,
	    sizeof(reply), &reply_length, &view) == NETSTACK_IGNORED);
	be16(udp + 6, 0);
	be16(udp + 4, 7);
	assert(netstack_process_frame(&stack, frame, length, 6, reply,
	    sizeof(reply), &reply_length, &view) == NETSTACK_IGNORED);
	be16(udp + 4, (uint16_t)(length - 14 - 20 + 1));
	assert(netstack_process_frame(&stack, frame, length, 7, reply,
	    sizeof(reply), &reply_length, &view) == NETSTACK_IGNORED);
	assert(!netstack_build_udp_frame(&stack, peer_mac, peer_ip, 0, 1,
	    request, sizeof(request) - 1, frame, sizeof(frame), &length));
	assert(!netstack_build_udp_frame(&stack, peer_mac, peer_ip, 1, 1,
	    request, NETSTACK_UDP_MAX_PAYLOAD + 1, frame, sizeof(frame), &length));
}

static void
test_ipv4_protocol_classification(void)
{
	struct netstack stack;
	uint8_t frame[NETSTACK_MAX_FRAME] = {0};
	static uint8_t maximum_payload[TCP_WIRE_SEGMENT_MAX -
	    TCP_WIRE_HEADER_MIN];
	uint8_t reply[NETSTACK_MAX_FRAME];
	uint8_t input_copy[NETSTACK_MAX_FRAME];
	enum netstack_ipv4_protocol protocol;
	enum netstack_event event;
	struct netstack_udp_view udp_view;
	size_t length, reply_length;
	assert(netstack_init(&stack, &config));
	length = ipv4_protocol_frame(frame, 1, 8);
	assert(netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	assert(protocol == NETSTACK_IPV4_ICMP);
	length = tcp_frame(frame, TCP_FLAG_SYN, NULL, 0, NULL, 0);
	assert(netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	assert(protocol == NETSTACK_IPV4_TCP);
	/* All receive outcomes clear the borrowed UDP view and produce no frame. */
	#define EXPECT_TCP_RESULT(expected_, parsed_) do { \
		size_t parser_calls_before = tcp_wire_parse_calls; \
		memset(reply, 0xa5, sizeof(reply)); \
		memcpy(input_copy, frame, length); \
		udp_view = (struct netstack_udp_view){.source_port = 0xffff}; \
		event = netstack_process_frame(&stack, frame, length, 1, reply, \
		    sizeof(reply), &reply_length, &udp_view); \
		assert(event == (expected_)); \
		assert(tcp_wire_parse_calls == parser_calls_before + \
		    ((parsed_) ? 1U : 0U)); \
		assert(reply_length == 0); \
		assert(memcmp(input_copy, frame, length) == 0); \
		assert(udp_view.source_port == 0 && udp_view.payload == NULL && \
		    udp_view.payload_length == 0); \
	} while (0)
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true); /* SYN */
	length = tcp_frame(frame, TCP_FLAG_ACK, NULL, 0, NULL, 0);
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true); /* ACK */
	static const uint8_t payload[] = {0x10, 0x20, 0x30};
	length = tcp_frame(frame, TCP_FLAG_ACK | TCP_FLAG_PSH, NULL, 0,
	    payload, sizeof(payload));
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true);
	static const uint8_t mss_options[] = {2, 4, 0x05, 0xb4};
	length = tcp_frame(frame, TCP_FLAG_SYN, mss_options,
	    sizeof(mss_options), NULL, 0);
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true);
	static const uint8_t unknown_options[] = {30, 4, 0xaa, 0xbb};
	length = tcp_frame(frame, TCP_FLAG_SYN, unknown_options,
	    sizeof(unknown_options), NULL, 0);
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true);
	/* Wire-valid but semantically unsupported flags are still classified as
	 * valid unhandled segments; no TCP feature is activated. */
	length = tcp_frame(frame, TCP_FLAG_SYN | TCP_FLAG_ECE, NULL, 0, NULL, 0);
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true);
	/* Bytes after IPv4 total_length are Ethernet padding, not TCP input. */
	length = tcp_frame(frame, TCP_FLAG_SYN, NULL, 0, NULL, 0);
	memset(frame + length, 0xa5, 6);
	length += 6;
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true);
	length = tcp_frame(frame, TCP_FLAG_SYN, NULL, 0, NULL, 0);
	frame[NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + 16] ^= 1;
	EXPECT_TCP_RESULT(NETSTACK_TCP_INVALID_CHECKSUM, true);
	length = ipv4_protocol_frame(frame, 6, 19);
	EXPECT_TCP_RESULT(NETSTACK_TCP_INVALID_HEADER, true);
	/* Invalid MSS length, with a valid TCP checksum, reports options distinctly. */
	length = ipv4_protocol_frame(frame, 6, 24);
	{
		uint8_t *tcp = frame + NETSTACK_ETHERNET_HEADER +
		    NETSTACK_IPV4_HEADER;
		be16(tcp, 443); be16(tcp + 2, 42000);
		tcp[12] = 0x60; tcp[13] = TCP_FLAG_SYN;
		be16(tcp + 14, 4096);
		tcp[20] = 2; tcp[21] = 3; tcp[22] = 5; tcp[23] = 0;
		repair_tcp_checksum(frame, 24);
	}
	EXPECT_TCP_RESULT(NETSTACK_TCP_INVALID_OPTIONS, true);
	/* IPv4 total length can validate while leaving a short TCP payload. */
	length = ipv4_protocol_frame(frame, 6, 10);
	EXPECT_TCP_RESULT(NETSTACK_TCP_INVALID_HEADER, true);
	length = tcp_frame(frame, TCP_FLAG_SYN, NULL, 0, NULL, 0);
	frame[NETSTACK_ETHERNET_HEADER + 10] ^= 1;
	EXPECT_TCP_RESULT(NETSTACK_IGNORED, false); /* Invalid IPv4 checksum. */
	length = ipv4_protocol_frame(frame, 17, 8);
	assert(netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	assert(protocol == NETSTACK_IPV4_UDP);
	length = ipv4_protocol_frame(frame, 99, 0);
	assert(netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	assert(protocol == NETSTACK_IPV4_UNSUPPORTED);
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	length = ipv4_protocol_frame(frame, 99, 0);
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	length = tcp_frame(frame, TCP_FLAG_SYN, NULL, 0, maximum_payload,
	    sizeof(maximum_payload));
	assert(length == NETSTACK_ETHERNET_HEADER + NETSTACK_MTU);
	EXPECT_TCP_RESULT(NETSTACK_TCP_DISABLED, true); /* 1480-byte segment. */
	/* 1481 TCP bytes require an IPv4 total length beyond the configured MTU,
	 * so the IPv4 validator rejects the packet before transport parsing. */
	length = ipv4_protocol_frame(frame, 6, TCP_WIRE_SEGMENT_MAX + 1U);
	EXPECT_TCP_RESULT(NETSTACK_IGNORED, false);
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER, 19,
	    &protocol));
	assert(!netstack_ipv4_classify(NULL, 20, &protocol));
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER, 20,
	    NULL));
	length = ipv4_protocol_frame(frame, 6, 20);
	frame[NETSTACK_ETHERNET_HEADER] = 0x14; /* wrong IP version */
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	length = ipv4_protocol_frame(frame, 6, 20);
	frame[NETSTACK_ETHERNET_HEADER] = 0x44; /* IHL below minimum */
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	length = ipv4_protocol_frame(frame, 6, 20);
	frame[NETSTACK_ETHERNET_HEADER] = 0x46; /* unsupported/truncated options */
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    NETSTACK_IPV4_HEADER, &protocol));
	length = ipv4_protocol_frame(frame, 6, 20);
	be16(frame + NETSTACK_ETHERNET_HEADER + 2, NETSTACK_MTU + 1);
	be16(frame + NETSTACK_ETHERNET_HEADER + 10, 0);
	be16(frame + NETSTACK_ETHERNET_HEADER + 10,
	    sum(frame + NETSTACK_ETHERNET_HEADER, NETSTACK_IPV4_HEADER));
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    NETSTACK_MAX_FRAME - NETSTACK_ETHERNET_HEADER, &protocol));
	length = ipv4_protocol_frame(frame, 6, 20);
	be16(frame + NETSTACK_ETHERNET_HEADER + 2, 19);
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	length = ipv4_protocol_frame(frame, 6, 20);
	be16(frame + NETSTACK_ETHERNET_HEADER + 2, 41);
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	length = ipv4_protocol_frame(frame, 6, 20);
	frame[NETSTACK_ETHERNET_HEADER + 10] ^= 1;
	assert(!netstack_ipv4_classify(frame + NETSTACK_ETHERNET_HEADER,
	    length - NETSTACK_ETHERNET_HEADER, &protocol));
	#undef EXPECT_TCP_RESULT
	puts("IPv4/TCP receive classification: valid discard, header/checksum/options errors, IPv4 gates PASS");
}

static void
test_arp(void)
{
	struct netstack stack;
	uint8_t frame[60] = {0}, reply[60], peer[4] = {10, 15, 3, 2};
	uint8_t mac[6];
	size_t reply_length;
	assert(netstack_init(&stack, &config));
	assert(netstack_process_frame(&stack, frame, 13, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	assert(arp_frame(frame, 1) == 42);
	assert(netstack_process_frame(&stack, frame, 42, 10, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_ARP_REPLY);
	assert(reply_length == 60 && reply[12] == 0x08 && reply[13] == 0x06);
	assert(reply[20] == 0 && reply[21] == 2);
	assert(netstack_arp_lookup(&stack, peer, 11, mac));
	assert(mac[5] == 2 && mac[4] == 3);
	assert(arp_frame(frame, 2) == 42);
	assert(netstack_process_frame(&stack, frame, 42, 12, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_ARP_RESOLVED);
	assert(netstack_arp_lookup(&stack, peer, 12 + NETSTACK_ARP_TTL_TICKS,
	    mac) == false);
	assert(arp_frame(frame, 1) == 42);
	frame[18] = 5;
	assert(netstack_process_frame(&stack, frame, 42, 20, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	for (uint8_t host = 2; host <= 10; host++) {
		assert(arp_frame(frame, 1) == 42);
		frame[31] = host;
		frame[27] = host;
		frame[6 + 5] = host;
		frame[14 + 8 + 5] = host;
		assert(netstack_process_frame(&stack, frame, 42, 30 + host, reply,
		    sizeof(reply), &reply_length, NULL) == NETSTACK_ARP_REPLY);
	}
	uint8_t first_host[4] = {10, 15, 3, 2};
	assert(!netstack_arp_lookup(&stack, first_host, 50, mac));
}

static void
test_ipv4_icmp(void)
{
	struct netstack stack;
	static const uint8_t marker[] = "bounded-echo";
	uint8_t frame[1518] = {0}, reply[1518];
	size_t length, reply_length;
	struct netstack_icmp_view view;
	uint8_t request[1518];
	assert(netstack_init(&stack, &config));
	length = ipv4_echo(frame, 8, marker, sizeof(marker) - 1);
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_ECHO_REQUEST);
	assert(reply_length == length && reply[34] == 0);
	assert(reply[38] == 0x12 && reply[39] == 0x34);
	assert(reply[42] == marker[0]);
	length = ipv4_echo(frame, 0, marker, sizeof(marker) - 1);
	assert(netstack_process_frame_ex(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL, &view) == NETSTACK_ECHO_REPLY);
	assert(view.type == 0 && view.code == 0 && view.identifier == 0x1234 &&
	    view.sequence == 7 && view.payload_length == sizeof(marker) - 1 &&
	    view.payload[0] == marker[0] && view.source_address[0] == 10 &&
	    view.source_address[3] == 2);
	length = ipv4_echo(request, 8, marker, sizeof(marker) - 1);
	length = ipv4_dest_unreachable(frame, request + 14, 28);
	assert(netstack_process_frame_ex(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL, &view) == NETSTACK_ICMP_ERROR);
	assert(view.type == 3 && view.code == 1 && view.quoted_type == 8 &&
	    view.quoted_identifier == 0x1234 && view.quoted_sequence == 7 &&
	    view.quoted_source[3] == 2 && view.quoted_destination[3] == 1);
	frame[14 + 10] ^= 1;
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	length = ipv4_echo(frame, 8, marker, sizeof(marker) - 1);
	frame[14 + 6] = 0x20; /* MF: fragmentation is unsupported. */
	be16(frame + 14 + 10, 0);
	be16(frame + 14 + 10, sum(frame + 14, 20));
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	assert(netstack_process_frame(&stack, frame, NETSTACK_MAX_FRAME + 1,
	    1, reply, sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	length = ipv4_echo(frame, 8, marker, sizeof(marker) - 1);
	frame[14] = 0x46; /* IP options are unsupported. */
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
	length = ipv4_echo(frame, 8, marker, sizeof(marker) - 1);
	frame[14 + 9] = 17; /* Truncated UDP header is rejected. */
	be16(frame + 14 + 10, 0);
	be16(frame + 14 + 10, sum(frame + 14, 20));
	assert(netstack_process_frame(&stack, frame, length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_IGNORED);
}

static void
test_builders(void)
{
	struct netstack stack;
	struct netstack_config invalid = config;
	uint8_t frame[1518], peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	uint8_t peer_ip[4] = {10, 15, 3, 2};
	static const uint8_t payload[] = {1, 2, 3};
	size_t length;
	assert(netstack_init(&stack, &config));
	for (size_t i = 0; i < 4; i++) invalid.netmask[i] = 0;
	assert(!netstack_init(&stack, &invalid));
	invalid = config;
	invalid.netmask[1] = 0;
	invalid.netmask[2] = 0xff;
	assert(!netstack_init(&stack, &invalid));
	assert(netstack_init(&stack, &config));
	assert(netstack_build_arp_request(&stack, peer_ip, frame, sizeof(frame),
	    &length) && length == 60);
	assert(frame[0] == 0xff && frame[12] == 0x08 && frame[13] == 0x06);
	assert(netstack_build_echo_request(&stack, peer_mac, peer_ip, 1, 2,
	    payload, sizeof(payload), frame, sizeof(frame), &length));
	assert(length == 45 && frame[12] == 0x08 && frame[13] == 0x00);
	assert(netstack_build_echo_request(&stack, peer_mac, peer_ip, 1, 2,
	    payload, sizeof(payload), frame, 40, &length) == false);
}

int
main(void)
{
	test_arp();
	test_ipv4_icmp();
	test_builders();
	test_udp();
	test_ipv4_protocol_classification();
	puts("Bounded Ethernet, ARP, IPv4, ICMP, and UDP tests passed");
	return 0;
}
