#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel/netstack.h"
#include "kernel/tcp_runtime.h"

static const struct netstack_config config = {
	.mac = {0x52, 0x54, 0x00, 0x15, 0x03, 0x01},
	.address = {10, 15, 3, 1},
	.netmask = {255, 255, 255, 0}
};
static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
static const uint8_t peer_ip[4] = {10, 15, 3, 2};

static void
put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static uint16_t
checksum(const uint8_t *p, size_t length)
{
	uint32_t sum = 0;
	while (length >= 2) {
		sum += ((uint16_t)p[0] << 8) | p[1];
		p += 2;
		length -= 2;
	}
	if (length != 0)
		sum += (uint16_t)p[0] << 8;
	while (sum >> 16)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

static size_t
make_syn(uint8_t *frame)
{
	uint8_t *ip = frame + NETSTACK_ETHERNET_HEADER;
	struct tcp_wire_spec spec = {
		.source_port = 41000,
		.destination_port = 8080,
		.sequence = 1234,
		.flags = TCP_FLAG_SYN,
		.window = 4096
	};
	size_t tcp_length;
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	put16(frame + 12, 0x0800);
	memset(ip, 0, NETSTACK_IPV4_HEADER);
	ip[0] = 0x45;
	put16(ip + 2, NETSTACK_IPV4_HEADER + TCP_WIRE_HEADER_MIN);
	ip[8] = 64;
	ip[9] = 6;
	memcpy(ip + 12, peer_ip, 4);
	memcpy(ip + 16, config.address, 4);
	put16(ip + 10, checksum(ip, NETSTACK_IPV4_HEADER));
	assert(tcp_wire_build(peer_ip, config.address, &spec,
	    ip + NETSTACK_IPV4_HEADER, TCP_WIRE_SEGMENT_MAX, &tcp_length) ==
	    TCP_WIRE_OK);
	assert(tcp_length == TCP_WIRE_HEADER_MIN);
	return NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + tcp_length;
}

int
main(void)
{
	struct netstack stack;
	struct tcp_connection_handle listener;
	uint8_t frame[NETSTACK_MAX_FRAME] = {0};
	uint8_t saved[NETSTACK_MAX_FRAME];
	uint8_t reply[NETSTACK_MAX_FRAME];
	size_t frame_length, reply_length;
	assert(tcp_runtime_init());
	assert(tcp_runtime_listener_open(config.address, 8080, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(netstack_init(&stack, &config));
	frame_length = make_syn(frame);
	memcpy(saved, frame, frame_length);
	assert(netstack_process_frame(&stack, frame, frame_length, 1, reply,
	    sizeof(reply), &reply_length, NULL) == NETSTACK_TCP_DISABLED);
	assert(reply_length == 0);
	assert(memcmp(frame, saved, frame_length) == 0);
	/* Successful shutdown proves the gated route retained and then released its
	 * listener reference. No child/event/action was created from the SYN. */
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));
	assert(tcp_runtime_shutdown());
	puts("TCP receive integration gate: validated SYN routed to listener, no core event/response, reference released PASS");
	return 0;
}
