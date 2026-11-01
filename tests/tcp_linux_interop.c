#define _GNU_SOURCE
#include "kernel/tcp_runtime.h"
#include "kernel/tcp_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FRAME_MAX 1514U
#define TEST_PORT 19090U
#define LOCAL_PORT 42000U
#define REQUEST_LENGTH 1800U
#define RESPONSE_LENGTH 1750U
#define DEADLINE_MS 20000U

static const uint8_t local_ip[4] = {10, 55, 0, 1};
static const uint8_t peer_ip[4] = {10, 55, 0, 2};
static uint16_t ip_id = 1;

static uint16_t
be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t
be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | p[3];
}

static void
put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static uint32_t
sum_bytes(uint32_t sum, const uint8_t *bytes, size_t length)
{
	while (length >= 2) {
		sum += be16(bytes);
		bytes += 2;
		length -= 2;
	}
	if (length != 0)
		sum += (uint16_t)bytes[0] << 8;
	return sum;
}

static bool
checksum_valid(uint32_t sum)
{
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)sum == 0xffffU;
}

/* Independent test checksum; this deliberately does not call tcp_wire_*(). */
static bool
tcp_checksum_valid(const uint8_t source[4], const uint8_t destination[4],
	const uint8_t *segment, size_t length)
{
	uint32_t sum = 0;
	if (length > UINT16_MAX)
		return false;
	sum = sum_bytes(sum, source, 4);
	sum = sum_bytes(sum, destination, 4);
	sum += 6;
	sum += (uint16_t)length;
	return checksum_valid(sum_bytes(sum, segment, length));
}

static uint16_t
ipv4_checksum(const uint8_t *header, size_t length)
{
	uint32_t sum = sum_bytes(0, header, length);
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

static uint64_t
now_ticks(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return (uint64_t)now.tv_sec * 100U + (uint64_t)now.tv_nsec / 10000000U;
}

static uint64_t
now_ms(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static void
fail(const char *what)
{
	perror(what);
	exit(1);
}

static bool
send_segment(int fd, int ifindex, const uint8_t source_mac[6],
	const uint8_t destination_mac[6], const uint8_t *segment, size_t length)
{
	uint8_t frame[FRAME_MAX] = {0};
	struct sockaddr_ll address = {0};
	uint8_t *ip = frame + ETH_HLEN;
	size_t total = 20U + length;
	if (length < TCP_WIRE_HEADER_MIN || length > TCP_WIRE_SEGMENT_MAX ||
	    total > 1500U || ETH_HLEN + total > sizeof(frame) ||
	    !tcp_checksum_valid(local_ip, peer_ip, segment, length))
		return false;
	memcpy(frame, destination_mac, 6);
	memcpy(frame + 6, source_mac, 6);
	put16(frame + 12, ETH_P_IP);
	ip[0] = 0x45;
	put16(ip + 2, (uint16_t)total);
	put16(ip + 4, ip_id++);
	put16(ip + 6, 0x4000); /* Don't Fragment. */
	ip[8] = 64;
	ip[9] = 6;
	memcpy(ip + 12, local_ip, 4);
	memcpy(ip + 16, peer_ip, 4);
	put16(ip + 10, ipv4_checksum(ip, 20));
	memcpy(ip + 20, segment, length);
	address.sll_family = AF_PACKET;
	address.sll_protocol = htons(ETH_P_IP);
	address.sll_ifindex = ifindex;
	address.sll_halen = 6;
	memcpy(address.sll_addr, destination_mac, 6);
	if (sendto(fd, frame, ETH_HLEN + total, 0,
	    (struct sockaddr *)&address, sizeof(address)) != (ssize_t)(ETH_HLEN + total))
		return false;
	return true;
}

static bool
drain_tx(int fd, int ifindex, const uint8_t source_mac[6],
	const uint8_t destination_mac[6], struct tcp_connection_handle handle)
{
	uint8_t segment[TCP_WIRE_SEGMENT_MAX];
	for (unsigned int i = 0; i < 4; i++) {
		struct tcp_runtime_test_tx_result tx;
		enum tcp_runtime_test_tx_status status =
		    tcp_runtime_test_next_segment(handle, segment, sizeof(segment), &tx);
		if (status == TCP_RUNTIME_TEST_TX_EMPTY)
			return true;
		if (status != TCP_RUNTIME_TEST_TX_OK || tx.length > sizeof(segment) ||
		    !send_segment(fd, ifindex, source_mac, destination_mac,
		    segment, tx.length))
			return false;
		printf("OS->Linux segment seq=%u ack=%u flags=0x%02x len=%zu checksum=valid\n",
		    be32(segment + 4), be32(segment + 8),
		    segment[13], tx.length);
	}
	return false;
}

static bool
receive_segment(int fd, uint64_t deadline_ms, uint8_t *segment,
	size_t capacity, size_t *length, struct tcp_wire_segment *view)
{
	uint8_t frame[FRAME_MAX];
	while (now_ms() < deadline_ms) {
		struct pollfd pfd = {.fd = fd, .events = POLLIN};
		int remaining = (int)(deadline_ms - now_ms());
		int polled = poll(&pfd, 1, remaining);
		if (polled < 0 && errno == EINTR)
			continue;
		if (polled <= 0)
			return false;
		ssize_t got = recv(fd, frame, sizeof(frame), 0);
		if (got < (ssize_t)(ETH_HLEN + 20) ||
		    be16(frame + 12) != ETH_P_IP)
			continue;
		const uint8_t *ip = frame + ETH_HLEN;
		size_t ihl = (size_t)(ip[0] & 0x0fU) * 4U;
		size_t ip_length = be16(ip + 2);
		if ((ip[0] >> 4) != 4 || ihl < 20 || ihl > 60 ||
		    ip_length < ihl + TCP_WIRE_HEADER_MIN ||
		    ETH_HLEN + ip_length > (size_t)got || ip[9] != 6 ||
		    memcmp(ip + 12, peer_ip, 4) != 0 ||
		    memcmp(ip + 16, local_ip, 4) != 0 ||
		    !checksum_valid(sum_bytes(0, ip, ihl)))
			continue;
		size_t tcp_length = ip_length - ihl;
		if (tcp_length > capacity ||
		    !tcp_checksum_valid(ip + 12, ip + 16, ip + ihl, tcp_length))
			continue;
		struct tcp_wire_pseudo_header pseudo = {0};
		memcpy(pseudo.source, ip + 12, 4);
		memcpy(pseudo.destination, ip + 16, 4);
		pseudo.protocol = 6;
		pseudo.segment_length = (uint16_t)tcp_length;
		if (tcp_wire_parse(&pseudo, ip + ihl, tcp_length, view) != TCP_WIRE_OK)
			continue;
		memcpy(segment, ip + ihl, tcp_length);
		*length = tcp_length;
		printf("Linux->OS segment seq=%u ack=%u flags=0x%02x win=%u payload=%zu checksums=valid\n",
		    view->sequence, view->acknowledgment, view->flags, view->window,
		    view->payload_length);
		return true;
	}
	return false;
}

static int
run_passive_handshake(int fd, int ifindex, const uint8_t local_mac[6],
	const uint8_t peer_mac[6])
{
	struct tcp_connection_handle listener;
	struct tcp_connection_handle child;
	struct tcp_runtime_test_input_result input;
	struct tcp_runtime_test_close_result close_result;
	uint8_t segment_bytes[TCP_WIRE_SEGMENT_MAX];
	size_t segment_length;
	struct tcp_wire_segment segment;
	uint64_t deadline = now_ms() + DEADLINE_MS;
	if (!tcp_runtime_init() || tcp_runtime_listener_open(local_ip, TEST_PORT,
	    &listener) != TCP_RUNTIME_LISTENER_OK)
		return 1;
	if (!receive_segment(fd, deadline, segment_bytes, sizeof(segment_bytes),
	    &segment_length, &segment) || segment.flags != TCP_FLAG_SYN ||
	    !segment.parsed_options.mss_present || segment.parsed_options.mss != 1460 ||
	    tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0x1655aa31U, now_ticks(), &input) !=
	    TCP_RUNTIME_TEST_INPUT_CHILD_CREATED || !input.has_reference)
		return 1;
	child = input.handle;
	printf("passive SYN routed to exact listener tuple; child state=%d\n",
	    input.outcome.state);
	if (!drain_tx(fd, ifindex, local_mac, peer_mac, child))
		return 1;
	if (!receive_segment(fd, deadline, segment_bytes, sizeof(segment_bytes),
	    &segment_length, &segment) || segment.flags != TCP_FLAG_ACK ||
	    tcp_runtime_test_process_segment(peer_ip, local_ip, &segment, 0,
	    now_ticks(), &input) != TCP_RUNTIME_TEST_INPUT_ESTABLISHED ||
	    !input.has_reference || input.outcome.state != TCP_STATE_ESTABLISHED)
		return 1;
	if (!tcp_runtime_handle_release(input.handle))
		return 1;
	puts("Linux active open and OpenSillicon passive child both established");
	if (!receive_segment(fd, deadline, segment_bytes, sizeof(segment_bytes),
	    &segment_length, &segment) || (segment.flags & TCP_FLAG_FIN) == 0 ||
	    tcp_runtime_test_process_segment(peer_ip, local_ip, &segment, 0,
	    now_ticks(), &input) != TCP_RUNTIME_TEST_INPUT_PROCESSED ||
	    input.outcome.state != TCP_STATE_CLOSE_WAIT ||
	    !drain_tx(fd, ifindex, local_mac, peer_mac, child))
		return 1;
	if (tcp_runtime_test_local_close(child, now_ticks(), &close_result) !=
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED ||
	    close_result.state != TCP_STATE_LAST_ACK ||
	    !drain_tx(fd, ifindex, local_mac, peer_mac, child))
		return 1;
	if (!receive_segment(fd, now_ms() + DEADLINE_MS, segment_bytes,
	    sizeof(segment_bytes), &segment_length, &segment) ||
	    (segment.flags & TCP_FLAG_ACK) == 0 ||
	    tcp_runtime_test_process_segment(peer_ip, local_ip, &segment, 0,
	    now_ticks(), &input) != TCP_RUNTIME_TEST_INPUT_PROCESSED ||
	    input.outcome.state != TCP_STATE_CLOSED)
		return 1;
	if (!tcp_runtime_handle_release(child) ||
	    !tcp_runtime_listener_close(listener) ||
	    !tcp_runtime_handle_release(listener) || !tcp_runtime_shutdown())
		return 1;
	puts("passive FIN/ACK teardown removed child link; listener and Runtime closed cleanly");
	return 0;
}

static bool
adversarial_input(struct tcp_connection_handle handle,
	const struct tcp_segment_spec *spec, size_t truncate,
	uint8_t mutation, struct tcp_runtime_test_active_result *result)
{
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
	size_t length;
	if (!tcp_segment_build_ipv4(peer_ip, local_ip, spec, bytes, sizeof(bytes),
	    &length))
		return false;
	if (mutation == 1)
		bytes[16] ^= 0x80; /* corrupt checksum */
	else if (mutation == 2)
		bytes[12] = 0x40; /* data offset below minimum */
	else if (mutation == 3) {
		bytes[12] = 0x60; /* 24-byte header with malformed MSS length */
		bytes[20] = 2;
		bytes[21] = 0;
		bytes[22] = 0;
		bytes[23] = 0;
		length = 24;
	}
	if (truncate != 0)
		length = truncate;
	return tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, now_ticks(), result) == (mutation == 0 && truncate == 0 ?
	    TCP_RUNTIME_TEST_ACTIVE_OK : TCP_RUNTIME_TEST_ACTIVE_MALFORMED);
}

static bool
run_adversarial_inputs(int fd, int ifindex, const uint8_t local_mac[6],
	const uint8_t peer_mac[6], struct tcp_connection_handle handle,
	const struct tcp_runtime_test_active_result *established)
{
	struct tcp_runtime_test_active_result result;
	struct tcp_runtime_test_core_result snapshot;
	uint8_t payload[] = {0x41};
	struct tcp_segment_spec spec = {
		.source_port = TEST_PORT,
		.destination_port = LOCAL_PORT,
		.sequence = established->rcv_nxt,
		.acknowledgment = established->snd_una,
		.flags = TCP_FLAG_ACK,
		.window = 4096
	};
	const uint32_t old_una = established->snd_una;
	const uint32_t old_nxt = established->snd_nxt;
	const uint32_t old_rcv = established->rcv_nxt;
	if (!adversarial_input(handle, &spec, 0, 1, &result) ||
	    !adversarial_input(handle, &spec, 19, 0, &result) ||
	    !adversarial_input(handle, &spec, 0, 2, &result) ||
	    !adversarial_input(handle, &spec, 0, 3, &result) ||
	    !tcp_runtime_test_snapshot(handle, &snapshot) ||
	    snapshot.state != TCP_STATE_ESTABLISHED || snapshot.snd_una != old_una ||
	    snapshot.snd_nxt != old_nxt || snapshot.rcv_nxt != old_rcv)
		return false;
	/* Future ACK and hostile reset sequence values enter through the real Wire
	 * parser; only this deterministic host test injects them without Linux. */
	spec.acknowledgment = old_nxt + 1;
	if (!adversarial_input(handle, &spec, 0, 0, &result) ||
	    result.state != TCP_STATE_ESTABLISHED ||
	    result.snd_una != old_una ||
	    (result.actions & TCP_ACTION_SEND_ACK) == 0 ||
	    !drain_tx(fd, ifindex, local_mac, peer_mac, handle))
		return false;
	spec.acknowledgment = old_una;
	spec.sequence = old_rcv + 1;
	spec.flags = TCP_FLAG_RST;
	if (!adversarial_input(handle, &spec, 0, 0, &result) ||
	    result.state != TCP_STATE_ESTABLISHED ||
	    (result.actions & TCP_ACTION_SEND_ACK) == 0 ||
	    !drain_tx(fd, ifindex, local_mac, peer_mac, handle))
		return false;
	spec.sequence = old_rcv + TCP_CORE_RX_CAPACITY + 1U;
	if (!adversarial_input(handle, &spec, 0, 0, &result) ||
	    result.state != TCP_STATE_ESTABLISHED || result.actions != 0 ||
	    !tcp_runtime_test_snapshot(handle, &snapshot) ||
	    snapshot.snd_una != old_una || snapshot.rcv_nxt != old_rcv)
		return false;
	spec.sequence = old_rcv;
	spec.flags = TCP_FLAG_SYN;
	if (!adversarial_input(handle, &spec, 0, 0, &result) ||
	    result.state != TCP_STATE_ESTABLISHED ||
	    (result.actions & TCP_ACTION_SEND_ACK) == 0 ||
	    !drain_tx(fd, ifindex, local_mac, peer_mac, handle))
		return false;
	spec.flags = TCP_FLAG_ACK;
	spec.sequence = old_rcv;
	spec.acknowledgment = old_una;
	spec.payload = payload;
	spec.payload_length = sizeof(payload);
	spec.sequence = old_rcv + TCP_CORE_RX_CAPACITY + 1U;
	if (!adversarial_input(handle, &spec, 0, 0, &result) ||
	    result.state != TCP_STATE_ESTABLISHED || result.accepted_bytes != 0 ||
	    result.rcv_nxt != old_rcv ||
	    !drain_tx(fd, ifindex, local_mac, peer_mac, handle) ||
	    !tcp_runtime_test_snapshot(handle, &snapshot) ||
	    snapshot.rcv_nxt != old_rcv)
		return false;
	puts("adversarial parser/Core inputs: checksum, truncation, offset, options, future ACK, RST window, unexpected SYN, out-of-window data PASS");
	return true;
}

int
main(int argc, char **argv)
{
	struct tcp_core_tuple tuple = {
		.local_address = {10, 55, 0, 1},
		.remote_address = {10, 55, 0, 2},
		.local_port = LOCAL_PORT,
		.remote_port = TEST_PORT
	};
	struct tcp_runtime_test_active_result active;
	struct tcp_runtime_test_io_result io;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_connection_handle handle;
	uint8_t local_mac[6];
	uint8_t peer_mac[6];
	uint8_t frame_segment[TCP_WIRE_SEGMENT_MAX];
	uint8_t request[REQUEST_LENGTH];
	uint8_t response[RESPONSE_LENGTH];
	uint8_t incoming[RESPONSE_LENGTH];
	size_t incoming_length = 0;
	size_t segment_length;
	uint32_t iss = 0x52a31f70U;
	if (argc != 4) {
		fprintf(stderr, "usage: %s IFNAME PEER_MAC active|passive\n", argv[0]);
		return 2;
	}
	unsigned int octets[6];
	if (sscanf(argv[2], "%x:%x:%x:%x:%x:%x", &octets[0], &octets[1],
	    &octets[2], &octets[3], &octets[4], &octets[5]) != 6)
		return 2;
	for (size_t i = 0; i < 6; i++) {
		if (octets[i] > 255)
			return 2;
		peer_mac[i] = (uint8_t)octets[i];
	}
	int ifindex = (int)if_nametoindex(argv[1]);
	if (ifindex == 0)
		fail("if_nametoindex");
	int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd < 0)
		fail("AF_PACKET socket (CAP_NET_RAW required in isolated user namespace)");
	struct sockaddr_ll bind_address = {.sll_family = AF_PACKET,
	    .sll_protocol = htons(ETH_P_ALL), .sll_ifindex = ifindex};
	if (bind(fd, (struct sockaddr *)&bind_address, sizeof(bind_address)) != 0)
		fail("AF_PACKET bind");
	struct ifreq request_if = {0};
	strncpy(request_if.ifr_name, argv[1], IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFHWADDR, &request_if) != 0)
		fail("SIOCGIFHWADDR");
	memcpy(local_mac, request_if.ifr_hwaddr.sa_data, 6);
	if (strcmp(argv[3], "passive") == 0) {
		int status = run_passive_handshake(fd, ifindex, local_mac, peer_mac);
		close(fd);
		return status;
	}
	if (strcmp(argv[3], "active") != 0)
		return 2;
	if (!tcp_runtime_init()) {
		fprintf(stderr, "Runtime init failed\n");
		return 1;
	}
	if (tcp_runtime_test_active_open(&tuple, iss, now_ticks(), &active) !=
	    TCP_RUNTIME_TEST_ACTIVE_OK || !active.has_reference) {
		fprintf(stderr, "Runtime active open failed\n");
		return 1;
	}
	handle = active.handle;
	if (!drain_tx(fd, ifindex, local_mac, peer_mac, handle)) {
		fprintf(stderr, "could not serialize/send initial SYN\n");
		return 1;
	}
	uint64_t deadline = now_ms() + DEADLINE_MS;
	for (;;) {
		struct tcp_wire_segment received;
		if (!receive_segment(fd, deadline, frame_segment, sizeof(frame_segment),
		    &segment_length, &received)) {
			fprintf(stderr, "timeout waiting for Linux SYN-ACK\n");
			return 1;
		}
		if (active.state == TCP_STATE_SYN_SENT &&
		    (!received.parsed_options.mss_present ||
		    received.parsed_options.mss != 1460)) {
			fprintf(stderr, "Linux SYN-ACK did not advertise expected veth MSS\n");
			return 1;
		}
		if (tcp_runtime_test_active_input(handle, peer_ip, local_ip,
		    frame_segment, segment_length, now_ticks(), &active) !=
		    TCP_RUNTIME_TEST_ACTIVE_OK) {
			fprintf(stderr, "Runtime rejected Linux handshake segment\n");
			return 1;
		}
		if (!drain_tx(fd, ifindex, local_mac, peer_mac, handle))
			fail("send handshake response");
		if (active.state == TCP_STATE_ESTABLISHED)
			break;
	}
	printf("handshake established: OpenSillicon=%d Linux=listener, local_mss=%u\n",
	    active.state, (unsigned int)TCP_CORE_LOCAL_MSS);
	if (!run_adversarial_inputs(fd, ifindex, local_mac, peer_mac, handle,
	    &active)) {
		fprintf(stderr, "adversarial packet injection changed connection state\n");
		return 1;
	}
	for (size_t i = 0; i < sizeof(request); i++)
		request[i] = (uint8_t)((i * 17U + 9U) & 0xffU);
	for (size_t i = 0; i < sizeof(response); i++)
		response[i] = (uint8_t)((i * 13U + 7U) & 0xffU);
	if (tcp_runtime_test_write(handle, request, sizeof(request), now_ticks(),
	    &io) != TCP_RUNTIME_TEST_IO_OK || io.bytes != sizeof(request)) {
		fprintf(stderr, "Runtime request write failed\n");
		return 1;
	}
	if (!drain_tx(fd, ifindex, local_mac, peer_mac, handle))
		fail("send application data");
	deadline = now_ms() + DEADLINE_MS;
	while (incoming_length < sizeof(incoming)) {
		struct tcp_wire_segment received;
		if (!receive_segment(fd, deadline, frame_segment, sizeof(frame_segment),
		    &segment_length, &received)) {
			fprintf(stderr, "timeout waiting for Linux data/ACK\n");
			return 1;
		}
		if (tcp_runtime_test_active_input(handle, peer_ip, local_ip,
		    frame_segment, segment_length, now_ticks(), &active) !=
		    TCP_RUNTIME_TEST_ACTIVE_OK || !drain_tx(fd, ifindex, local_mac,
		    peer_mac, handle))
			fail("process Linux segment");
		if (received.payload_length != 0) {
			if (received.payload_length > sizeof(incoming) - incoming_length) {
				fprintf(stderr, "Linux response exceeded bounded fixture\n");
				return 1;
			}
			if (tcp_runtime_test_read(handle, incoming + incoming_length,
			    sizeof(incoming) - incoming_length, &io) != TCP_RUNTIME_TEST_IO_OK)
				fail("Runtime read");
			incoming_length += io.bytes;
		}
	}
	if (memcmp(incoming, response, sizeof(response)) != 0) {
		fprintf(stderr, "Linux response bytes differ\n");
		return 1;
	}
	printf("bidirectional payloads verified: request=%zu response=%zu exact\n",
	    sizeof(request), sizeof(response));
	if (tcp_runtime_test_local_close(handle, now_ticks(), &close_result) !=
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED || !drain_tx(fd, ifindex, local_mac,
	    peer_mac, handle))
		fail("local FIN");
	deadline = now_ms() + DEADLINE_MS;
	bool time_wait = false;
	while (!time_wait) {
		struct tcp_wire_segment received;
		if (!receive_segment(fd, deadline, frame_segment, sizeof(frame_segment),
		    &segment_length, &received)) {
			fprintf(stderr, "timeout waiting for Linux FIN/ACK\n");
			return 1;
		}
		if (tcp_runtime_test_active_input(handle, peer_ip, local_ip,
		    frame_segment, segment_length, now_ticks(), &active) !=
		    TCP_RUNTIME_TEST_ACTIVE_OK || !drain_tx(fd, ifindex, local_mac,
		    peer_mac, handle))
			fail("process close segment");
		time_wait = active.state == TCP_STATE_TIME_WAIT;
	}
	struct tcp_runtime_timer_result timers;
	if (tcp_runtime_service_timers(now_ticks() + TCP_CORE_TIME_WAIT_TICKS,
	    4, &timers) != TCP_RUNTIME_TIMER_OK ||
	    !tcp_runtime_handle_release(handle) || !tcp_runtime_shutdown()) {
		fprintf(stderr, "Runtime cleanup failed after TIME-WAIT\n");
		return 1;
	}
	close(fd);
	puts("Linux TCP interoperability PASS: SYN/SYN-ACK/ACK, MSS/window, multi-segment bidirectional data, FIN/TIME-WAIT cleanup");
	return 0;
}
