#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/tcp_core.h"

static const uint8_t local_ip[4] = {10, 15, 3, 1};
static const uint8_t peer_ip[4] = {10, 15, 3, 2};

static struct tcp_core_tuple active_tuple(void);

static const char *
state_name(enum tcp_connection_state state)
{
	static const char *const names[] = {
		"CLOSED", "LISTEN", "SYN-SENT", "SYN-RECEIVED", "ESTABLISHED",
		"FIN-WAIT-1", "FIN-WAIT-2", "CLOSE-WAIT", "LAST-ACK", "CLOSING",
		"TIME-WAIT"
	};
	return (unsigned)state < sizeof(names) / sizeof(names[0]) ?
	    names[state] : "INVALID";
}

static const char *
event_name(enum tcp_core_event_type type)
{
	static const char *const names[] = {
		"LISTEN", "ACTIVE-OPEN", "SEGMENT", "APPLICATION-CLOSE",
		"RETRANSMIT-TIMEOUT", "TIME-WAIT-EXPIRE"
	};
	return (unsigned)type < sizeof(names) / sizeof(names[0]) ?
	    names[type] : "INVALID";
}

static bool
step_report(struct tcp_connection_table *table,
	struct tcp_connection_handle handle, const struct tcp_core_event *event,
	struct tcp_core_result *result)
{
	bool accepted = tcp_core_step(table, handle, event, result);
	if (!accepted) {
		printf("TCP event=%s rejected\n", event_name(event->type));
		return false;
	}
	printf("TCP event=%s state=%s snd_una=%08x snd_nxt=%08x "
	    "rcv_nxt=%08x snd_wnd=%u rcv_wnd=%u actions=%03x accepted=%zu\n",
	    event_name(event->type), state_name(result->state), result->snd_una,
	    result->snd_nxt, result->rcv_nxt, result->send_window,
	    result->receive_window, result->actions, result->accepted_bytes);
	return true;
}

static void
service_report(const struct tcp_core_service_result *service)
{
	for (size_t i = 0; i < service->count; i++) {
		const struct tcp_core_service_item *item = &service->items[i];
		const struct tcp_core_result *result = &item->result;
		printf("TCP service slot=%u generation=%u state=%s retries=%u "
		    "deadline=%llu interval=%llu actions=%03x more_due=%u\n",
		    item->handle.slot, item->handle.generation,
		    state_name(result->state), result->retransmissions,
		    (unsigned long long)result->timer_deadline,
		    (unsigned long long)result->timer_interval, result->actions,
		    service->more_due);
	}
}

static void
put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static uint16_t
get16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t
sum_bytes(uint32_t sum, const uint8_t *data, size_t length)
{
	while (length >= 2) {
		sum += get16(data);
		data += 2;
		length -= 2;
	}
	if (length != 0)
		sum += (uint16_t)data[0] << 8;
	while (sum >> 16)
		sum = (sum & 0xffffU) + (sum >> 16);
	return sum;
}

static void
repair_checksum(uint8_t *segment, size_t length)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < 4; i += 2) {
		sum += get16(peer_ip + i);
		sum += get16(local_ip + i);
	}
	sum += 6U + (uint16_t)length;
	segment[16] = 0;
	segment[17] = 0;
	sum = sum_bytes(sum, segment, length);
	put16(segment + 16, (uint16_t)~sum);
}

static void
repair_ipv4_checksum(uint8_t *ip)
{
	ip[10] = 0;
	ip[11] = 0;
	put16(ip + 10, (uint16_t)~sum_bytes(0, ip, 20));
}

static bool
test_address_equal(const uint8_t *a, const uint8_t *b)
{
	for (size_t i = 0; i < 4; i++)
		if (a[i] != b[i])
			return false;
	return true;
}

static bool
build_ipv4_fixture(const struct tcp_core_tuple *tuple,
	const struct tcp_segment_spec *spec, uint8_t *packet, size_t capacity,
	size_t *packet_length)
{
	size_t tcp_length;
	if (tuple == NULL || spec == NULL || packet == NULL ||
	    packet_length == NULL || capacity < 20 ||
	    !tcp_segment_build_ipv4(tuple->remote_address, tuple->local_address,
	    spec, packet + 20, capacity - 20, &tcp_length) || tcp_length > 1480)
		return false;
	for (size_t i = 0; i < 20; i++)
		packet[i] = 0;
	packet[0] = 0x45;
	put16(packet + 2, (uint16_t)(20 + tcp_length));
	packet[8] = 64;
	packet[9] = 6;
	for (size_t i = 0; i < 4; i++) {
		packet[12 + i] = tuple->remote_address[i];
		packet[16 + i] = tuple->local_address[i];
	}
	repair_ipv4_checksum(packet);
	*packet_length = 20 + tcp_length;
	return true;
}

static bool
parse_ipv4_fixture(const uint8_t *packet, size_t available,
	const uint8_t source[4], const uint8_t destination[4],
	struct tcp_segment_view *view)
{
	uint16_t total;
	if (packet == NULL || source == NULL || destination == NULL ||
	    view == NULL || available < 20 || (packet[0] >> 4) != 4 ||
	    (packet[0] & 0x0fU) != 5 || packet[9] != 6)
		return false;
	total = get16(packet + 2);
	if (total < 40 || total > TCP_CORE_MAX_SEGMENT + 20U ||
	    total != available || sum_bytes(0, packet, 20) != 0xffffU ||
	    !test_address_equal(packet + 12, source) ||
	    !test_address_equal(packet + 16, destination))
		return false;
	return tcp_segment_parse_ipv4(source, destination, packet + 20,
	    total - 20, view);
}

static bool
build_segment(uint8_t *bytes, size_t capacity, size_t *length, uint8_t flags,
	uint32_t sequence, uint32_t acknowledgment, const uint8_t *payload,
	size_t payload_length, bool mss)
{
	const struct tcp_segment_spec spec = {
		.source_port = 40000,
		.destination_port = 8080,
		.sequence = sequence,
		.acknowledgment = acknowledgment,
		.flags = flags,
		.window = 4096,
		.include_mss = mss,
		.mss = 1200,
		.payload = payload,
		.payload_length = payload_length
	};
	return tcp_segment_build_ipv4(peer_ip, local_ip, &spec, bytes, capacity,
	    length);
}

static void
test_wire(void)
{
	static const uint8_t payload[] = "tcp-wire";
	uint8_t bytes[TCP_CORE_MAX_SEGMENT];
	struct tcp_segment_view view;
	size_t length;
	assert(build_segment(bytes, sizeof(bytes), &length,
	    TCP_FLAG_SYN | TCP_FLAG_ACK, 0x10203040, 0x50607080,
	    payload, sizeof(payload) - 1, true));
	assert(length == 32 && (bytes[12] >> 4) == 6);
	assert(tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	assert(view.source_port == 40000 && view.destination_port == 8080);
	assert(view.sequence == 0x10203040 && view.acknowledgment == 0x50607080);
	assert(view.flags == (TCP_FLAG_SYN | TCP_FLAG_ACK));
	assert(view.mss_present && view.mss == 1200);
	assert(view.payload_length == sizeof(payload) - 1);
	for (size_t i = 0; i < view.payload_length; i++)
		assert(view.payload[i] == payload[i]);
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, 19, &view));
	bytes[12] = 0x40;
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	bytes[12] = 0x70; /* data offset extends beyond the supplied segment */
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	bytes[12] = 0x60;
	bytes[12] |= 1; /* reserved/NS bit */
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	bytes[12] &= 0xf0;
	bytes[length - 1] ^= 0x80;
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	bytes[length - 1] ^= 0x80;
	bytes[21] = 3; /* MSS option has an invalid length */
	repair_checksum(bytes, length);
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	bytes[21] = 4;
	bytes[20] = 30; /* unknown, well-formed option must be ignored */
	repair_checksum(bytes, length);
	assert(tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view) &&
	    !view.mss_present);
	bytes[20] = 2; /* restore a structurally valid header */
	bytes[0] = bytes[1] = 0; /* reserved TCP port */
	repair_checksum(bytes, length);
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length, &view));
	assert(!tcp_segment_parse_ipv4(peer_ip, local_ip, bytes, length - 1,
	    &view));
	assert(!tcp_segment_parse_ipv4(local_ip, peer_ip, bytes, length, &view));
	puts("TCP wire tests: pseudoheader checksum, MSS, bounds, options, flags PASS");
}

static void
test_ipv4_packet_fixtures(void)
{
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_segment_spec spec = {
		.source_port = 8080, .destination_port = 40000,
		.sequence = 0xfffffffeU, .acknowledgment = 0xffffffffU,
		.flags = TCP_FLAG_SYN | TCP_FLAG_ACK, .window = 4096
	};
	struct tcp_segment_view view;
	uint8_t packet[1500];
	size_t length;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet), &length));
	assert(parse_ipv4_fixture(packet, length, peer_ip, local_ip, &view));
	assert(view.sequence == 0xfffffffeU && view.acknowledgment == 0xffffffffU &&
	    view.flags == (TCP_FLAG_SYN | TCP_FLAG_ACK));
	/* TCP checksum and wrong IPv4 pseudoheader addresses are rejected. */
	packet[36] ^= 1;
	assert(!parse_ipv4_fixture(packet, length, peer_ip, local_ip, &view));
	packet[36] ^= 1;
	assert(!parse_ipv4_fixture(packet, length, local_ip, peer_ip, &view));
	/* Truncated IP/TCP header, invalid data offset, and bad IP totals fail at
	 * their respective validation boundaries. */
	assert(!parse_ipv4_fixture(packet, 19, peer_ip, local_ip, &view));
	packet[32] = 0x40;
	repair_checksum(packet + 20, length - 20);
	assert(!parse_ipv4_fixture(packet, length, peer_ip, local_ip, &view));
	packet[32] = 0x50;
	repair_checksum(packet + 20, length - 20);
	put16(packet + 2, 39);
	repair_ipv4_checksum(packet);
	assert(!parse_ipv4_fixture(packet, length, peer_ip, local_ip, &view));
	put16(packet + 2, (uint16_t)(length + 1));
	repair_ipv4_checksum(packet);
	assert(!parse_ipv4_fixture(packet, length, peer_ip, local_ip, &view));
	puts("TCP IPv4 fixtures: valid SYN-ACK, TCP/pseudoheader checksum, truncation, data offset, and IP total-length rejection PASS");
}

static struct tcp_core_tuple
active_tuple(void)
{
	struct tcp_core_tuple tuple = {
		.local_address = {10, 15, 3, 1},
		.remote_address = {10, 15, 3, 2},
		.local_port = 40000,
		.remote_port = 8080
	};
	return tuple;
}

static struct tcp_core_event
segment_event(const struct tcp_segment_view *segment, uint64_t now)
{
	struct tcp_core_event event = {
		.type = TCP_EVENT_SEGMENT,
		.now_tick = now,
		.segment = segment
	};
	for (size_t i = 0; i < 4; i++) {
		event.source_address[i] = peer_ip[i];
		event.destination_address[i] = local_ip[i];
	}
	return event;
}

static struct tcp_segment_view
make_view(uint32_t sequence, uint32_t acknowledgment, uint8_t flags,
	const uint8_t *payload, size_t payload_length)
{
	struct tcp_segment_view segment = {
		.source_port = 8080,
		.destination_port = 40000,
		.sequence = sequence,
		.acknowledgment = acknowledgment,
		.flags = flags,
		.window = 4096,
		.payload = payload,
		.payload_length = payload_length
	};
	return segment;
}

static void
test_active_state_and_sequence(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_result result;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_event event = {
		.type = TCP_EVENT_ACTIVE_OPEN,
		.initial_sequence = 0xfffffffeU
	};
	struct tcp_segment_view segment;
	static const uint8_t data[] = "abc";
	uint8_t readback[16];
	size_t read_length;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT && result.snd_una == 0xfffffffeU &&
	    result.snd_nxt == 0xffffffffU && result.actions == TCP_ACTION_SEND_SYN);
	segment = make_view(0xfffffffeU, 0xffffffffU,
	    TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	segment.source_port = 8080;
	segment.destination_port = 40000;
	segment.mss_present = true;
	segment.mss = 1200;
	event = segment_event(&segment, 10);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.snd_una == 0xffffffffU &&
	    result.rcv_nxt == 0xffffffffU && result.send_window ==
	    TCP_CORE_SEND_WINDOW_LIMIT && result.actions == TCP_ACTION_SEND_ACK);
	/* An out-of-window ACK cannot update sequence or advertised window state. */
	segment = make_view(0xffffffffU + TCP_CORE_RX_CAPACITY + 1U,
	    0xffffffffU, TCP_FLAG_ACK, NULL, 0);
	segment.window = 7;
	event = segment_event(&segment, 10);
	assert(step_report(&table, handle, &event, &result));
	assert(result.snd_una == 0xffffffffU &&
	    result.send_window == TCP_CORE_SEND_WINDOW_LIMIT &&
	    result.actions == TCP_ACTION_SEND_ACK);
	/* A duplicate SYN-ACK gets an ACK but does not rewind sequence state. */
	segment = make_view(0xfffffffeU, 0xffffffffU,
	    TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	segment.mss_present = true;
	segment.mss = 1200;
	event = segment_event(&segment, 10);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED &&
	    result.actions == TCP_ACTION_SEND_ACK && result.rcv_nxt == 0xffffffffU);
	segment = make_view(0xffffffffU, 0xffffffffU, TCP_FLAG_ACK | TCP_FLAG_PSH,
	    data, sizeof(data) - 1);
	event = segment_event(&segment, 11);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.rcv_nxt == 2 &&
	    result.accepted_bytes == 3 &&
	    result.actions == (TCP_ACTION_DELIVER_DATA | TCP_ACTION_SEND_ACK));
	assert(tcp_core_connection_read(&table, handle, readback, sizeof(readback),
	    &read_length));
	assert(read_length == 3 && readback[0] == 'a' && readback[2] == 'c');
	segment = make_view(3, 0xffffffffU, TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 12);
	assert(step_report(&table, handle, &event, &result));
	assert(result.rcv_nxt == 2 && result.actions == 0);
	segment = make_view(2, 0xffffffffU, TCP_FLAG_ACK, NULL, 1);
	event = segment_event(&segment, 12);
	assert(!step_report(&table, handle, &event, &result));
	assert(tcp_core_connection_get(&table, handle, &result) &&
	    result.rcv_nxt == 2 && result.receive_window == TCP_CORE_RX_CAPACITY);
	segment = make_view(3 + TCP_CORE_RX_CAPACITY, 0xffffffffU, TCP_FLAG_ACK,
	    NULL, 0);
	event = segment_event(&segment, 13);
	assert(step_report(&table, handle, &event, &result));
	assert(result.rcv_nxt == 2 && result.actions == TCP_ACTION_SEND_ACK);
	segment = make_view(1, 0xffffffffU, TCP_FLAG_ACK | TCP_FLAG_PSH,
	    data, 1); /* one byte before RCV.NXT is old/out of order */
	event = segment_event(&segment, 13);
	assert(step_report(&table, handle, &event, &result));
	assert(result.rcv_nxt == 2 && result.accepted_bytes == 0 &&
	    result.actions == TCP_ACTION_SEND_ACK);
	segment = make_view(2, 0, TCP_FLAG_ACK, NULL, 0); /* ACK beyond SND.NXT */
	event = segment_event(&segment, 14);
	assert(step_report(&table, handle, &event, &result));
	assert(result.rcv_nxt == 2 && result.actions == TCP_ACTION_SEND_ACK);
	segment = make_view(2, 0xfffffffeU, TCP_FLAG_ACK | TCP_FLAG_PSH,
	    data, 1); /* old ACK does not block in-order data */
	event = segment_event(&segment, 14);
	assert(step_report(&table, handle, &event, &result));
	assert(result.rcv_nxt == 3 && result.snd_una == 0xffffffffU &&
	    result.accepted_bytes == 1 &&
	    result.actions == (TCP_ACTION_DELIVER_DATA | TCP_ACTION_SEND_ACK));
	segment = make_view(3 + TCP_CORE_RX_CAPACITY, 0xffffffffU,
	    TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 15);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED &&
	    (result.actions & TCP_ACTION_ABORT) == 0);
	segment = make_view(3 + TCP_CORE_RX_CAPACITY, 0xffffffffU,
	    TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 15);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.rcv_nxt == 3 &&
	    result.actions == TCP_ACTION_SEND_ACK);
	segment = make_view(3, 0xffffffffU, TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 16);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    (result.actions & TCP_ACTION_ABORT) != 0);
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 123};
	assert(!step_report(&table, handle, &event, &result));
	assert(tcp_core_connection_release(&table, handle));
	assert(!tcp_core_connection_get(&table, handle, &result));
	puts("TCP active model: wraparound SYN/SYN-ACK, ACK bounds, data order, RST PASS");
}

static void
test_isolated_active_passive_handshakes(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle active, listener, child;
	struct tcp_core_result result, child_result;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_event event;
	struct tcp_segment_spec spec;
	struct tcp_segment_view view;
	uint8_t packet[1500];
	static const uint8_t early_data[] = {'x'};
	size_t packet_length;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &active));
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = UINT32_MAX, .now_tick = 20};
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT && result.snd_nxt == 0 &&
	    result.actions == TCP_ACTION_SEND_SYN && result.timer_armed);

	spec = (struct tcp_segment_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = UINT32_MAX,
		.acknowledgment = UINT32_MAX, /* expected ACK is zero */
		.flags = TCP_FLAG_SYN | TCP_FLAG_ACK,
		.window = 4096
	};
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 21);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT &&
	    result.actions == TCP_ACTION_SEND_RST &&
	    result.timer_armed); /* wrong SYN-ACK acknowledgment */

	spec.acknowledgment = 0;
	spec.flags |= TCP_FLAG_FIN; /* illegal control combination */
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 21);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT && result.actions == 0);

	/* Data cannot be accepted before the active handshake completes. */
	spec.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
	spec.payload = early_data;
	spec.payload_length = sizeof(early_data);
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 21);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT && result.actions == 0 &&
	    result.rcv_nxt == 0);
	spec.payload = NULL;
	spec.payload_length = 0;

	spec.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 22);
	event.source_address[3]++; /* tuple mismatch cannot advance the handshake */
	assert(!tcp_core_step(&table, active, &event, &result));
	event = segment_event(&view, 22);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.snd_una == 0 &&
	    result.snd_nxt == 0 && result.rcv_nxt == 0 &&
	    result.actions == TCP_ACTION_SEND_ACK && !result.timer_armed);

	/* Out-of-window and in-window non-exact RSTs cannot close. The in-window
	 * case gets a challenge ACK; an exact RCV.NXT RST closes. */
	spec = (struct tcp_segment_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = TCP_CORE_RX_CAPACITY,
		.flags = TCP_FLAG_RST,
		.window = 4096
	};
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 23);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.actions == 0);
	spec.sequence = 1;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 24);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED &&
	    result.actions == TCP_ACTION_SEND_ACK);
	spec.sequence = 0;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 24);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    (result.actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_core_connection_release(&table, active));
	assert(!tcp_core_step(&table, active, &event, &result));

	/* Simultaneous open: bare SYN in active SYN-SENT, then peer SYN-ACK. */
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &active));
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = UINT32_MAX - 2U, .now_tick = 40};
	assert(step_report(&table, active, &event, &result));
	spec = (struct tcp_segment_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = UINT32_MAX,
		.flags = TCP_FLAG_SYN,
		.window = 1024
	};
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 41);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_RECEIVED && result.rcv_nxt == 0 &&
	    result.snd_nxt == UINT32_MAX - 1U && result.actions ==
	    (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
	spec.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
	spec.acknowledgment = UINT32_MAX - 1U;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 42);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.snd_una ==
	    UINT32_MAX - 1U && result.rcv_nxt == 0 &&
	    result.actions == TCP_ACTION_SEND_ACK && !result.timer_armed);
	event = segment_event(&view, 43); /* duplicate SYN-ACK */
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED &&
	    result.actions == TCP_ACTION_SEND_ACK && result.rcv_nxt == 0);
	spec = (struct tcp_segment_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = 0,
		.acknowledgment = UINT32_MAX - 1U,
		.flags = TCP_FLAG_ACK
	};
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 44); /* duplicate final ACK */
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.actions == 0);
	spec.flags = TCP_FLAG_RST;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 45);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_CLOSED);
	assert(tcp_core_connection_release(&table, active));

	/* Active-origin SYN-RECEIVED accepts a valid-sequence RST based on SEQ,
	 * not on the RST's ACK field. */
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &active));
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 10, .now_tick = 50};
	assert(step_report(&table, active, &event, &result));
	spec = (struct tcp_segment_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = 100,
		.flags = TCP_FLAG_SYN
	};
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 51);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_RECEIVED);
	spec.sequence = 101;
	spec.acknowledgment = 1;
	spec.flags = TCP_FLAG_RST | TCP_FLAG_ACK;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 52);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    (result.actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_core_connection_release(&table, active));
	/* SYN-SENT accepts reset only when the acknowledgment covers its SYN. */
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &active));
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 40};
	assert(step_report(&table, active, &event, &result));
	spec = (struct tcp_segment_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = 1,
		.acknowledgment = 40,
		.flags = TCP_FLAG_RST | TCP_FLAG_ACK
	};
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 25);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT && result.actions == 0);
	spec.acknowledgment = 41;
	spec.flags = TCP_FLAG_RST | TCP_FLAG_ACK | TCP_FLAG_FIN;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 25);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT && result.actions == 0);
	spec.acknowledgment = 41;
	spec.flags = TCP_FLAG_RST | TCP_FLAG_ACK;
	assert(build_ipv4_fixture(&tuple, &spec, packet, sizeof(packet),
	    &packet_length));
	assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip, &view));
	event = segment_event(&view, 26);
	assert(step_report(&table, active, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    (result.actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_core_connection_release(&table, active));

	/* Passive open keeps its listener, bounds children, retransmits a duplicate
	 * SYN, and accepts only the exact final ACK and sequence. */
	tuple = (struct tcp_core_tuple){
		.local_address = {10, 15, 3, 1},
		.local_port = 8080
	};
	assert(tcp_core_connection_create(&table, &tuple, &listener));
	event = (struct tcp_core_event){.type = TCP_EVENT_LISTEN};
	assert(step_report(&table, listener, &event, &result));
	assert(result.state == TCP_STATE_LISTEN);
	{
		struct tcp_core_tuple wire_tuple = active_tuple();
		wire_tuple.local_port = tuple.local_port;
		wire_tuple.remote_port = 40000;
		wire_tuple.remote_address[3] = 2;
		spec = (struct tcp_segment_spec){
			.source_port = wire_tuple.remote_port,
			.destination_port = wire_tuple.local_port,
			.sequence = UINT32_MAX,
			.flags = TCP_FLAG_SYN,
			.window = 2048
		};
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 30);
		event.initial_sequence = UINT32_MAX - 1U;
		assert(step_report(&table, listener, &event, &result));
		assert(result.state == TCP_STATE_LISTEN && result.has_child &&
		    result.actions == (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
		child = result.child;
		assert(tcp_core_connection_get(&table, child, &child_result));
		assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
		    child_result.snd_nxt == UINT32_MAX && child_result.rcv_nxt == 0 &&
		    child_result.timer_armed);
		assert(step_report(&table, listener, &event, &result));
		assert(!result.has_child && result.actions ==
		    (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
		assert(tcp_core_connection_count(&table) == 2);

		/* SYN or FIN combined with the final ACK is not accepted as the
		 * third handshake leg. */
		spec.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
		spec.sequence = 0;
		spec.acknowledgment = UINT32_MAX;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet, sizeof(packet),
		    &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 31);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
		    child_result.actions == 0 && child_result.timer_armed);
		spec.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet, sizeof(packet),
		    &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 31);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
		    child_result.actions == 0 && child_result.timer_armed);

		spec.flags = TCP_FLAG_ACK;
		spec.sequence = 0;
		spec.acknowledgment = UINT32_MAX - 1U; /* incorrect ACK */
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 31);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
		    child_result.actions == TCP_ACTION_SEND_RST &&
		    child_result.timer_armed);

		spec.flags = TCP_FLAG_RST;
		spec.sequence = 1; /* outside SYN-RECEIVED's exact RCV.NXT */
		spec.acknowledgment = 0;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 32);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
		    child_result.actions == 0);
		spec.flags = TCP_FLAG_RST | TCP_FLAG_ACK | TCP_FLAG_FIN;
		spec.sequence = 0;
		spec.acknowledgment = UINT32_MAX;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet, sizeof(packet),
		    &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 32);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
		    child_result.actions == 0);

		spec.flags = TCP_FLAG_ACK;
		spec.sequence = 0;
		spec.acknowledgment = UINT32_MAX;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 33);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_ESTABLISHED &&
		    child_result.snd_una == UINT32_MAX && child_result.rcv_nxt == 0 &&
		    child_result.actions == 0 && !child_result.timer_armed);
		assert(tcp_core_connection_get(&table, listener, &result) &&
		    result.state == TCP_STATE_LISTEN);
		spec.flags = TCP_FLAG_RST;
		spec.sequence = 0;
		spec.acknowledgment = 0;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 34);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_CLOSED &&
		    (child_result.actions & TCP_ACTION_ABORT) != 0);
		event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
		assert(step_report(&table, child, &event, &child_result));
		assert(tcp_core_connection_release(&table, child));

		/* A valid RST in SYN-RECEIVED closes only that child, not listener. */
		wire_tuple.remote_port = 40002;
		spec = (struct tcp_segment_spec){
			.source_port = wire_tuple.remote_port,
			.destination_port = wire_tuple.local_port,
			.sequence = 77,
			.flags = TCP_FLAG_SYN,
			.window = 2048
		};
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 35);
		event.initial_sequence = 91;
		assert(step_report(&table, listener, &event, &result));
		assert(result.has_child && result.state == TCP_STATE_LISTEN);
		child = result.child;
		spec.flags = TCP_FLAG_RST;
		spec.sequence = 78;
		assert(build_ipv4_fixture(&wire_tuple, &spec, packet,
		    sizeof(packet), &packet_length));
		assert(parse_ipv4_fixture(packet, packet_length, peer_ip, local_ip,
		    &view));
		event = segment_event(&view, 36);
		assert(step_report(&table, child, &event, &child_result));
		assert(child_result.state == TCP_STATE_CLOSED &&
		    (child_result.actions & TCP_ACTION_ABORT) != 0);
		assert(tcp_core_connection_release(&table, child));
		assert(tcp_core_connection_get(&table, listener, &result) &&
		    result.state == TCP_STATE_LISTEN);
		event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
		assert(step_report(&table, listener, &event, &result));
		assert(tcp_core_connection_release(&table, listener));
		assert(tcp_core_connection_count(&table) == 0);
	}
	puts("TCP isolated active/passive handshake: SYN/SYN-ACK/ACK, strict ACK/sequence checks, duplicate SYN, RST validation, wraparound, refs and cleanup PASS");
}

static void
assert_application_close_noop(struct tcp_connection_table *table,
	struct tcp_connection_handle handle, enum tcp_connection_state expected_state,
	uint64_t now_tick)
{
	struct tcp_core_result before, after;
	struct tcp_core_event event = {
		.type = TCP_EVENT_APPLICATION_CLOSE,
		.now_tick = now_tick
	};
	struct tcp_core_connection *connection = &table->slots[handle.slot];
	uint64_t deadline = connection->retransmit_deadline;
	uint64_t interval = connection->retransmit_interval;
	uint64_t time_wait_deadline = connection->time_wait_deadline;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint8_t retransmissions = connection->retransmissions;
	bool timer_armed = connection->timer_armed;
	assert(tcp_core_connection_get(table, handle, &before));
	assert(before.state == expected_state);
	snd_nxt = before.snd_nxt;
	rcv_nxt = before.rcv_nxt;
	assert(step_report(table, handle, &event, &after));
	assert(after.state == expected_state && after.actions == 0);
	assert(after.snd_nxt == snd_nxt && after.rcv_nxt == rcv_nxt);
	assert(connection->timer_armed == timer_armed);
	assert(connection->retransmit_deadline == deadline);
	assert(connection->retransmit_interval == interval);
	assert(connection->time_wait_deadline == time_wait_deadline);
	assert(connection->retransmissions == retransmissions);
}

static void
test_close_wait_last_ack_and_closing(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result result;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 1000};
	struct tcp_segment_view segment;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(2000, 1001, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 1);
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(2001, 1001, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 2);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSE_WAIT && result.rcv_nxt == 2002);
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_LAST_ACK &&
	    result.actions == (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK));
	assert_application_close_noop(&table, handle, TCP_STATE_LAST_ACK, 3);
	segment = make_view(2002, result.snd_una, TCP_FLAG_FIN | TCP_FLAG_ACK,
	    NULL, 0);
	event = segment_event(&segment, 3);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_LAST_ACK && result.rcv_nxt == 2002 &&
	    result.actions == TCP_ACTION_SEND_ACK);
	segment = make_view(2002, result.snd_nxt, TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 4);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSED);
	assert(tcp_core_connection_release(&table, handle));

	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 3000};
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(4000, 3001, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 4);
	assert(step_report(&table, handle, &event, &result));
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_FIN_WAIT_1);
	assert_application_close_noop(&table, handle, TCP_STATE_FIN_WAIT_1, 4);
	segment = make_view(4001, 3001, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 5);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSING && result.rcv_nxt == 4002);
	assert_application_close_noop(&table, handle, TCP_STATE_CLOSING, 5);
	segment = make_view(4002, 3002, TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 6);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_TIME_WAIT &&
	    (result.actions & TCP_ACTION_ENTER_TIME_WAIT) != 0);
	assert_application_close_noop(&table, handle, TCP_STATE_TIME_WAIT, 6);
	event = (struct tcp_core_event){.type = TCP_EVENT_TIME_WAIT_EXPIRE,
	    .now_tick = 6 + TCP_CORE_TIME_WAIT_TICKS};
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSED);
	assert_application_close_noop(&table, handle, TCP_STATE_CLOSED, event.now_tick);
	assert(tcp_core_connection_release(&table, handle));
	puts("TCP close states: CLOSE-WAIT/LAST-ACK and CLOSING/TIME-WAIT PASS");
}

static void
test_passive_close_and_time_wait(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle listener, child_handle;
	struct tcp_core_result result, child_result;
	uint64_t original_time_wait_deadline;
	struct tcp_core_tuple tuple = {
		.local_address = {10, 15, 3, 1},
		.local_port = 8080
	};
	struct tcp_core_event event = {.type = TCP_EVENT_LISTEN};
	struct tcp_segment_view segment = make_view(100, 0, TCP_FLAG_SYN, NULL, 0);
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &listener));
	assert(step_report(&table, listener, &event, &result));
	assert(result.state == TCP_STATE_LISTEN);
	segment.source_port = 40000;
	segment.destination_port = 8080;
	event = segment_event(&segment, 100);
	event.initial_sequence = 500;
	assert(step_report(&table, listener, &event, &result));
	assert(result.state == TCP_STATE_LISTEN && result.has_child &&
	    result.actions == (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
	child_handle = result.child;
	assert(tcp_core_connection_get(&table, child_handle, &child_result));
	assert(child_result.state == TCP_STATE_SYN_RECEIVED &&
	    child_result.snd_nxt == 501 && child_result.rcv_nxt == 101);
	assert(tcp_core_connection_count(&table) == 2);
	/* Duplicate SYN at the listener retransmits rather than allocating again. */
	event = segment_event(&segment, 101);
	event.initial_sequence = 999;
	assert(step_report(&table, listener, &event, &result));
	assert(!result.has_child && result.actions ==
	    (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
	assert(tcp_core_connection_count(&table) == 2);
	/* Duplicate SYN retransmits the same SYN-ACK without allocating a child. */
	event = segment_event(&segment, 101);
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_SYN_RECEIVED && !child_result.has_child &&
	    child_result.actions == (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN |
	    TCP_ACTION_SEND_ACK));
	segment = make_view(101, 501, TCP_FLAG_ACK, NULL, 0);
	segment.source_port = 40000;
	segment.destination_port = 8080;
	event = segment_event(&segment, 102);
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_ESTABLISHED && child_result.snd_una == 501);
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_FIN_WAIT_1 &&
	    child_result.actions == (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK));
	segment = make_view(101, 502, TCP_FLAG_ACK, NULL, 0);
	segment.source_port = 40000;
	segment.destination_port = 8080;
	event = segment_event(&segment, 103);
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_FIN_WAIT_2);
	assert_application_close_noop(&table, child_handle, TCP_STATE_FIN_WAIT_2, 103);
	segment = make_view(101, 502, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
	segment.source_port = 40000;
	segment.destination_port = 8080;
	event = segment_event(&segment, UINT64_MAX - 5);
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_TIME_WAIT &&
	    child_result.rcv_nxt == 102 &&
	    (child_result.actions & TCP_ACTION_ENTER_TIME_WAIT) != 0);
	original_time_wait_deadline = event.now_tick + TCP_CORE_TIME_WAIT_TICKS;
	/* The exact duplicate FIN is ACKed and restarts the 2MSL interval. */
	event = segment_event(&segment, original_time_wait_deadline - 1U);
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_TIME_WAIT &&
	    child_result.actions == TCP_ACTION_SEND_ACK);
	event = (struct tcp_core_event){.type = TCP_EVENT_TIME_WAIT_EXPIRE,
	    .now_tick = original_time_wait_deadline};
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_TIME_WAIT);
	event.now_tick = original_time_wait_deadline - 2U +
	    TCP_CORE_TIME_WAIT_TICKS;
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_TIME_WAIT);
	event.now_tick++;
	assert(step_report(&table, child_handle, &event, &child_result));
	assert(child_result.state == TCP_STATE_CLOSED);
	assert(tcp_core_connection_release(&table, child_handle));
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, listener, &event, &result));
	assert(result.state == TCP_STATE_CLOSED);
	assert(tcp_core_connection_release(&table, listener));
	puts("TCP passive model: bounded backlog, duplicate SYN, FIN close, TIME-WAIT PASS");
}

static void
test_bounded_send_retransmit_and_window(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result result;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 100};
	struct tcp_segment_view segment;
	uint8_t bytes[TCP_CORE_TX_CAPACITY];
	uint8_t copied[TCP_CORE_TX_CAPACITY];
	uint8_t encoded[TCP_CORE_MAX_SEGMENT];
	size_t copied_length;
	size_t encoded_length;
	uint32_t copied_sequence;
	struct tcp_segment_view parsed;
	static const uint8_t payload[] = "abcdefghij";
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(tcp_core_step(&table, handle, &event, &result));
	segment = make_view(900, 101, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	segment.mss_present = true;
	segment.mss = 4;
	segment.window = 6;
	event = segment_event(&segment, 1);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_ESTABLISHED && result.send_window == 6);
	assert(tcp_core_connection_write(&table, handle, payload,
	    sizeof(payload) - 1, 20, &result));
	assert(result.accepted_bytes == sizeof(payload) - 1 &&
	    result.actions == TCP_ACTION_SEND_DATA && result.snd_nxt == 105 &&
	    result.timer_armed && result.timer_deadline == 120);
	assert(tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(copied_length == 4 && copied_sequence == 101 &&
	    copied[0] == 'a' && copied[3] == 'd');
	assert(tcp_core_connection_build_outstanding_data(&table, handle, encoded,
	    sizeof(encoded), &encoded_length));
	assert(tcp_segment_parse_ipv4(local_ip, peer_ip, encoded, encoded_length,
	    &parsed));
	assert(parsed.source_port == tuple.local_port &&
	    parsed.destination_port == tuple.remote_port && parsed.sequence == 101 &&
	    parsed.acknowledgment == 901 && parsed.flags == TCP_FLAG_ACK &&
	    parsed.payload_length == 4 && parsed.payload[0] == 'a' &&
	    parsed.payload[3] == 'd');

	/* Partial ACK releases only acknowledged bytes. The remaining exact suffix
	 * stays outstanding and is the data selected for timeout retransmission.
	 * A zero-length ACK at RCV.NXT+1 is still within the receive window. */
	segment = make_view(902, 103, TCP_FLAG_ACK, NULL, 0);
	segment.window = 6;
	event = segment_event(&segment, 21);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.snd_una == 103 && result.timer_armed &&
	    tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(copied_length == 2 && copied_sequence == 103 &&
	    copied[0] == 'c' && copied[1] == 'd');
	event = (struct tcp_core_event){.type = TCP_EVENT_RETRANSMIT_TIMEOUT,
	    .now_tick = result.timer_deadline};
	assert(tcp_core_step(&table, handle, &event, &result));
	assert((result.actions & (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_DATA)) ==
	    (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_DATA));
	assert(tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(copied_length == 2 && copied_sequence == 103 && copied[0] == 'c');

	/* A stale sequence-space window advertisement cannot replace the newer
	 * window update, though its acceptable ACK still advances SND.UNA. */
	segment = make_view(901, 105, TCP_FLAG_ACK, NULL, 0);
	segment.window = 0;
	event = segment_event(&segment, result.timer_deadline + 1);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.actions == TCP_ACTION_SEND_DATA && result.snd_una == 105 &&
	    result.snd_nxt == 109 && result.timer_armed && result.send_window == 6);
	assert(tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(copied_length == 4 && copied_sequence == 105 && copied[0] == 'e' &&
	    copied[3] == 'h');

	/* An out-of-window segment cannot ACK data or reclaim its queue bytes. */
	segment = make_view(900 + TCP_CORE_RX_CAPACITY + 1U, 109, TCP_FLAG_ACK,
	    NULL, 0);
	segment.window = 0;
	event = segment_event(&segment, 200);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.snd_una == 105 && result.actions == TCP_ACTION_SEND_ACK);
	assert(tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence) && copied_length == 4);
	/* The final ACK closes this flight but advertises a zero window, so queued
	 * bytes wait without consuming sequence space or arming a timer. */
	segment = make_view(903, 109, TCP_FLAG_ACK, NULL, 0);
	segment.window = 0;
	event = segment_event(&segment, 202);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.snd_una == 109 && result.snd_nxt == 109 &&
	    result.send_window == 0 && !result.timer_armed && result.actions == 0);
	segment.sequence = 903;
	segment.acknowledgment = 109;
	segment.window = 2; /* a valid window update resumes only two bytes */
	event = segment_event(&segment, 203);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.actions == TCP_ACTION_SEND_DATA && result.snd_nxt == 111);
	assert(tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(copied_length == 2 && copied_sequence == 109 && copied[0] == 'i' &&
	    copied[1] == 'j');
	segment.acknowledgment = 111;
	segment.window = 0;
	event = segment_event(&segment, 204);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.snd_una == 111 && result.actions == 0 && !result.timer_armed);

	/* Queue capacity is fixed and overflow is all-or-nothing. */
	for (size_t i = 0; i < sizeof(bytes); i++)
		bytes[i] = (uint8_t)i;
	assert(tcp_core_connection_write(&table, handle, bytes, sizeof(bytes),
	    205, &result));
	assert(result.accepted_bytes == TCP_CORE_TX_CAPACITY &&
	    result.actions == 0 && result.send_window == 0);
	assert(!tcp_core_connection_write(&table, handle, payload, 1, 206, &result));
	assert(tcp_core_connection_get(&table, handle, &result) &&
	    result.state == TCP_STATE_ESTABLISHED && result.snd_una == 111);
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE,
	    .now_tick = 207};
	assert(!tcp_core_step(&table, handle, &event, &result));
	assert(tcp_core_connection_get(&table, handle, &result) &&
	    result.state == TCP_STATE_ESTABLISHED && result.snd_una == 111);
	assert(tcp_core_connection_release(&table, handle));
	/* A lost data ACK follows the same bounded exponential retry policy; the
	 * exact outstanding bytes remain serializable until retry exhaustion. */
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 300};
	assert(tcp_core_step(&table, handle, &event, &result));
	segment = make_view(1200, 301, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	segment.destination_port = tuple.local_port;
	segment.mss_present = true;
	segment.mss = 4;
	segment.window = 4;
	event = segment_event(&segment, 1);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(tcp_core_connection_write(&table, handle, payload, 4, 10, &result));
	for (size_t retry = 1; retry <= TCP_CORE_RETRY_LIMIT; retry++) {
		event = (struct tcp_core_event){
			.type = TCP_EVENT_RETRANSMIT_TIMEOUT,
			.now_tick = result.timer_deadline
		};
		assert(tcp_core_step(&table, handle, &event, &result));
		assert(result.state == TCP_STATE_ESTABLISHED &&
		    result.retransmissions == retry &&
		    (result.actions & (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_DATA)) ==
		    (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_DATA));
		assert(tcp_core_connection_copy_outstanding(&table, handle, copied,
		    sizeof(copied), &copied_length, &copied_sequence));
		assert(copied_length == 4 && copied_sequence == 301 &&
		    copied[0] == 'a' && copied[3] == 'd');
	}
	event.now_tick = result.timer_deadline;
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    (result.actions & TCP_ACTION_ABORT) != 0 &&
	    !tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(tcp_core_connection_release(&table, handle));

	/* The data queue and partial ACK offsets use modulo-2^32 sequence math. */
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 0xfffffffeU};
	assert(tcp_core_step(&table, handle, &event, &result));
	segment = make_view(200, 0xffffffffU, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	segment.destination_port = tuple.local_port;
	segment.mss_present = true;
	segment.mss = 4;
	segment.window = 4;
	event = segment_event(&segment, 1);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(tcp_core_connection_write(&table, handle, payload, 3, 2, &result));
	assert(result.snd_nxt == 2);
	segment = make_view(201, 0, TCP_FLAG_ACK, NULL, 0);
	segment.destination_port = tuple.local_port;
	event = segment_event(&segment, 3);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.snd_una == 0 && result.snd_nxt == 2 &&
	    tcp_core_connection_copy_outstanding(&table, handle, copied,
	    sizeof(copied), &copied_length, &copied_sequence));
	assert(copied_sequence == 0 && copied_length == 2 && copied[0] == 'b' &&
	    copied[1] == 'c');
	segment.acknowledgment = 2;
	event = segment_event(&segment, 4);
	assert(tcp_core_step(&table, handle, &event, &result));
	assert(result.snd_una == 2 && !result.timer_armed);
	assert(tcp_core_step(&table, handle,
	    &(struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE}, &result));
	assert(result.state == TCP_STATE_FIN_WAIT_1 &&
	    (result.actions & TCP_ACTION_SEND_FIN) != 0);
	assert(tcp_core_connection_release(&table, handle));
	puts("TCP bounded send queue, MSS/window segmentation, partial ACK, retransmit PASS");
}

static void
test_receive_bound_and_retry_limit(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result result;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 10};
	struct tcp_segment_view segment;
	uint8_t payload[TCP_CORE_RX_CAPACITY];
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(50, 11, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 1);
	assert(step_report(&table, handle, &event, &result));
	for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)i;
	segment = make_view(51, 11, TCP_FLAG_ACK | TCP_FLAG_PSH,
	    payload, sizeof(payload) / 2);
	event = segment_event(&segment, 2);
	assert(step_report(&table, handle, &event, &result));
	assert(result.receive_window == TCP_CORE_RX_CAPACITY / 2);
	segment.sequence += (uint32_t)segment.payload_length;
	event = segment_event(&segment, 3);
	assert(step_report(&table, handle, &event, &result));
	assert(result.receive_window == 0 && result.accepted_bytes ==
	    TCP_CORE_RX_CAPACITY / 2);
	segment.sequence += (uint32_t)segment.payload_length;
	event = segment_event(&segment, 4);
	assert(step_report(&table, handle, &event, &result));
	assert(result.receive_window == 0 && result.accepted_bytes == 0 &&
	    (result.actions & TCP_ACTION_DELIVER_DATA) == 0);
	{
		size_t read_count;
		assert(tcp_core_connection_read(&table, handle, payload,
		    TCP_CORE_RX_CAPACITY / 2, &read_count));
		assert(read_count == TCP_CORE_RX_CAPACITY / 2);
		assert(tcp_core_connection_get(&table, handle, &result));
		assert(result.receive_window == TCP_CORE_RX_CAPACITY / 2);
	}
	assert(step_report(&table, handle, &event, &result));
	assert(result.receive_window == 0 &&
	    result.accepted_bytes == TCP_CORE_RX_CAPACITY / 2);
	assert(tcp_core_connection_read(&table, handle, payload, sizeof(payload),
	    &(size_t){0}));
	assert(tcp_core_connection_release(&table, handle));

	event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 20};
	/* The first connection is still table-owned and ESTABLISHED, so reserve a
	 * distinct tuple for the timeout case. */
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	event = (struct tcp_core_event){.type = TCP_EVENT_RETRANSMIT_TIMEOUT,
	    .now_tick = TCP_CORE_RTO_INITIAL_TICKS};
	for (size_t i = 0; i < TCP_CORE_RETRY_LIMIT; i++) {
		assert(step_report(&table, handle, &event, &result));
		assert(result.state == TCP_STATE_SYN_SENT &&
		    (result.actions & TCP_ACTION_RETRANSMIT) != 0 &&
		    result.retransmissions == i + 1);
		event.now_tick = result.timer_deadline;
	}
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    (result.actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_core_connection_release(&table, handle));
	puts("TCP bounded receive window and five-retry timeout: PASS");
}

static void
test_time_wait_reset_validation(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result result;
	struct tcp_segment_view segment;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 100};
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(10, 101, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 1);
	assert(step_report(&table, handle, &event, &result));
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE,
	    .now_tick = 2};
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(11, 102, TCP_FLAG_ACK, NULL, 0);
	event = segment_event(&segment, 3);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_FIN_WAIT_2);
	segment.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
	event = segment_event(&segment, 4);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_TIME_WAIT && result.rcv_nxt == 12);
	segment = make_view(13, 0, TCP_FLAG_RST, NULL, 0);
	event = segment_event(&segment, 5);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_TIME_WAIT &&
	    result.actions == TCP_ACTION_SEND_ACK);
	segment.sequence = 12;
	event = segment_event(&segment, 6);
	assert(step_report(&table, handle, &event, &result));
	assert(result.state == TCP_STATE_CLOSED &&
	    result.actions == TCP_ACTION_ABORT);
	assert(tcp_core_connection_release(&table, handle));
	puts("TCP TIME-WAIT RST: challenge in-window non-exact reset, close on exact RCV.NXT PASS");
}

static void
test_table_lifetime_and_backlog(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handles[TCP_CORE_CONNECTION_LIMIT];
	struct tcp_connection_handle replacement;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result snapshot;
	tcp_core_table_init(&table);
	/* A just-created reservation is not table-owned until its start event.
	 * A failed Runtime start that releases this sole caller reference must not
	 * leave an allocated slot behind. */
	{
		struct tcp_connection_handle reserved, reclaimed;
		tuple.local_port = 39999;
		assert(tcp_core_connection_create(&table, &tuple, &reserved));
		assert(!table.slots[reserved.slot].table_owned &&
		    table.slots[reserved.slot].external_references == 1);
		assert(tcp_core_connection_count(&table) == 1);
		assert(tcp_core_connection_release(&table, reserved));
		assert(tcp_core_connection_count(&table) == 0);
		assert(!tcp_core_connection_get(&table, reserved, &snapshot));
		assert(tcp_core_connection_create(&table, &tuple, &reclaimed));
		assert(reclaimed.slot == reserved.slot &&
		    reclaimed.generation != reserved.generation);
		assert(tcp_core_connection_release(&table, reclaimed));
		assert(tcp_core_connection_count(&table) == 0);
	}
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++) {
		tuple.local_port = (uint16_t)(40000 + i);
		assert(tcp_core_connection_create(&table, &tuple, &handles[i]));
	}
	assert(tcp_core_connection_count(&table) == TCP_CORE_CONNECTION_LIMIT);
	tuple.local_port = 45000;
	assert(!tcp_core_connection_create(&table, &tuple, &replacement));
	assert(tcp_core_connection_retain(&table, handles[0]));
	assert(tcp_core_connection_release(&table, handles[0]));
	assert(tcp_core_connection_get(&table, handles[0], &snapshot));
	assert(tcp_core_connection_release(&table, handles[0]));
	assert(!tcp_core_connection_get(&table, handles[0], &snapshot));
	assert(tcp_core_connection_create(&table, &tuple, &replacement));
	assert(replacement.generation != handles[0].generation);
	for (size_t i = 1; i < TCP_CORE_CONNECTION_LIMIT; i++)
		assert(tcp_core_connection_release(&table, handles[i]));
	assert(tcp_core_connection_release(&table, replacement));
	assert(tcp_core_connection_count(&table) == 0);
	puts("TCP connection table: eight slots, exhaustion, retained handles, generation-safe reuse PASS");
}

static void
test_tcp_service_budget_and_cancellation(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handles[5];
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result result;
	struct tcp_core_service_result service;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 100};
	tcp_core_table_init(&table);
	assert(!tcp_core_live_isn_available());
	assert(tcp_core_connection_create(&table, &tuple, &handles[0]));
	assert(step_report(&table, handles[0], &event, &result));
	assert(result.timer_armed && result.timer_interval ==
	    TCP_CORE_RTO_INITIAL_TICKS && result.timer_deadline ==
	    TCP_CORE_RTO_INITIAL_TICKS);
	assert(tcp_core_service(&table, TCP_CORE_RTO_INITIAL_TICKS - 1, 1,
	    &service));
	assert(service.count == 0 && !service.more_due);
	{
		struct tcp_segment_view syn_ack = make_view(500, 101,
		    TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
		event = segment_event(&syn_ack, TCP_CORE_RTO_INITIAL_TICKS);
		assert(step_report(&table, handles[0], &event, &result));
	}
	assert(!result.timer_armed);
	assert(tcp_core_service(&table, UINT64_MAX, 1, &service));
	assert(service.count == 0 && !service.more_due);
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, handles[0], &event, &result));
	{
		struct tcp_segment_view ack = make_view(501, result.snd_nxt,
		    TCP_FLAG_ACK, NULL, 0);
		event = segment_event(&ack, 1);
		assert(step_report(&table, handles[0], &event, &result));
		assert(result.state == TCP_STATE_FIN_WAIT_2 && !result.timer_armed);
		ack = make_view(501, result.snd_nxt, TCP_FLAG_FIN | TCP_FLAG_ACK,
		    NULL, 0);
		event = segment_event(&ack, 1);
		assert(step_report(&table, handles[0], &event, &result));
		assert(result.state == TCP_STATE_TIME_WAIT);
	}
	assert(tcp_core_service(&table, 1 + TCP_CORE_TIME_WAIT_TICKS, 1,
	    &service));
	assert(service.count == 1 && service.items[0].result.state ==
	    TCP_STATE_CLOSED);
	service_report(&service);
	assert(tcp_core_connection_release(&table, service.items[0].handle));
	assert(tcp_core_connection_release(&table, handles[0]));

	tcp_core_table_init(&table);
	for (size_t i = 0; i < 5; i++) {
		tuple.local_port = (uint16_t)(40000 + i);
		event = (struct tcp_core_event){.type = TCP_EVENT_ACTIVE_OPEN,
		    .initial_sequence = (uint32_t)(200 + i), .now_tick = 1000};
		assert(tcp_core_connection_create(&table, &tuple, &handles[i]));
		assert(step_report(&table, handles[i], &event, &result));
	}
	assert(tcp_core_service(&table, 1099, 2, &service));
	assert(service.count == 0 && !service.more_due);
	assert(!tcp_core_service(&table, 1100,
	    TCP_CORE_SERVICE_EVENT_LIMIT + 1U, &service));
	assert(tcp_core_service(&table, 1100, 2, &service));
	assert(service.count == 2 && service.more_due);
	service_report(&service);
	for (size_t i = 0; i < service.count; i++) {
		assert(service.items[i].result.state == TCP_STATE_SYN_SENT &&
		    service.items[i].result.retransmissions == 1 &&
		    service.items[i].result.timer_interval ==
		    2 * TCP_CORE_RTO_INITIAL_TICKS &&
		    service.items[i].result.timer_deadline == 1300 &&
		    (service.items[i].result.actions & TCP_ACTION_RETRANSMIT) != 0);
		assert(tcp_core_connection_release(&table, service.items[i].handle));
	}
	assert(tcp_core_service(&table, 1100, 2, &service));
	assert(service.count == 2 && service.more_due);
	service_report(&service);
	for (size_t i = 0; i < service.count; i++)
		assert(tcp_core_connection_release(&table, service.items[i].handle));
	assert(tcp_core_service(&table, 1100, 2, &service));
	assert(service.count == 1 && !service.more_due);
	service_report(&service);
	assert(tcp_core_connection_release(&table, service.items[0].handle));
	for (size_t i = 0; i < 5; i++) {
		event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
		assert(step_report(&table, handles[i], &event, &result));
		assert(tcp_core_connection_release(&table, handles[i]));
	}
	assert(tcp_core_connection_count(&table) == 0);
	puts("TCP service budget: two events per pass, backoff and cancellation PASS");
}

static void
test_timer_pending_handle_lifetime(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_result result, snapshot;
	struct tcp_core_service_result service;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 900, .now_tick = 50};
	uint64_t now = 50 + TCP_CORE_RTO_INITIAL_TICKS;
	static const uint64_t intervals[] = {200, 400, 800, 800, 800};
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	assert(tcp_core_connection_release(&table, handle));
	for (size_t retry = 1; retry <= TCP_CORE_RETRY_LIMIT; retry++) {
		assert(tcp_core_service(&table, now, 1, &service));
		assert(service.count == 1);
		service_report(&service);
		assert(service.items[0].result.state == TCP_STATE_SYN_SENT &&
		    service.items[0].result.retransmissions == retry &&
		    service.items[0].result.timer_interval == intervals[retry - 1] &&
		    (service.items[0].result.actions & TCP_ACTION_RETRANSMIT) != 0);
		now = service.items[0].result.timer_deadline;
		assert(tcp_core_connection_release(&table, service.items[0].handle));
	}
	assert(tcp_core_service(&table, now, 1, &service));
	assert(service.count == 1 && service.items[0].result.state ==
	    TCP_STATE_CLOSED && (service.items[0].result.actions &
	    TCP_ACTION_ABORT) != 0);
	service_report(&service);
	assert(tcp_core_connection_count(&table) == 1);
	assert(tcp_core_connection_get(&table, handle, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_core_connection_release(&table, service.items[0].handle));
	assert(tcp_core_connection_count(&table) == 0);
	puts("TCP pending timer: table ref, service ref, retry exhaustion, release PASS");
}

static void
test_retransmit_timer_wraparound(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 77, .now_tick = UINT64_MAX - 50};
	struct tcp_core_result result;
	struct tcp_core_service_result service;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	assert(result.timer_deadline == 49 && result.timer_interval ==
	    TCP_CORE_RTO_INITIAL_TICKS);
	assert(tcp_core_service(&table, 48, 1, &service));
	assert(service.count == 0 && !service.more_due);
	assert(tcp_core_service(&table, 49, 1, &service));
	assert(service.count == 1 && service.items[0].result.state ==
	    TCP_STATE_SYN_SENT && service.items[0].result.timer_deadline == 249 &&
	    service.items[0].result.timer_interval == 200 &&
	    service.items[0].result.retransmissions == 1);
	service_report(&service);
	assert(tcp_core_connection_release(&table, service.items[0].handle));
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, handle, &event, &result));
	assert(tcp_core_connection_release(&table, handle));
	puts("TCP retransmit deadline across uint64 wrap: PASS");
}

static void
test_listener_backlog_limit(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle listener;
	struct tcp_connection_handle children[TCP_CORE_LISTEN_BACKLOG];
	struct tcp_core_tuple tuple = {
		.local_address = {10, 15, 3, 1},
		.local_port = 8080
	};
	struct tcp_core_result result;
	struct tcp_core_event event = {.type = TCP_EVENT_LISTEN};
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &listener));
	assert(step_report(&table, listener, &event, &result));
	{
		struct tcp_segment_view bad_ack = make_view(10, 11, TCP_FLAG_ACK,
		    NULL, 0);
		bad_ack.source_port = 40000;
		bad_ack.destination_port = 8080;
		event = segment_event(&bad_ack, 99);
		assert(step_report(&table, listener, &event, &result));
		assert(result.state == TCP_STATE_LISTEN &&
		    result.actions == TCP_ACTION_SEND_RST &&
		    tcp_core_connection_count(&table) == 1);
	}
	for (size_t i = 0; i < TCP_CORE_LISTEN_BACKLOG + 1U; i++) {
		struct tcp_segment_view segment = make_view((uint32_t)(100 + i), 0,
		    TCP_FLAG_SYN, NULL, 0);
		segment.source_port = (uint16_t)(41000 + i);
		segment.destination_port = 8080;
		event = segment_event(&segment, i);
		event.initial_sequence = (uint32_t)(700 + i);
		assert(step_report(&table, listener, &event, &result));
		if (i < TCP_CORE_LISTEN_BACKLOG) {
			assert(result.has_child && result.actions ==
			    (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
			children[i] = result.child;
		} else {
			assert(!result.has_child && result.actions == TCP_ACTION_SEND_RST);
		}
	}
	assert(tcp_core_connection_count(&table) == TCP_CORE_LISTEN_BACKLOG + 1U);
	for (size_t i = 0; i < TCP_CORE_LISTEN_BACKLOG; i++) {
		event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
		assert(step_report(&table, children[i], &event, &result));
		assert(result.state == TCP_STATE_CLOSED &&
		    (result.actions & TCP_ACTION_ABORT) != 0);
		assert(tcp_core_connection_release(&table, children[i]));
	}
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, listener, &event, &result));
	assert(tcp_core_connection_release(&table, listener));
	assert(tcp_core_connection_count(&table) == 0);
	puts("TCP listener backlog: four SYN children, fifth rejected, cleanup PASS");
}

static void
test_tuple_lookup_and_duplicate_rules(void)
{
	struct tcp_connection_table table;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_tuple query = tuple;
	struct tcp_core_tuple snapshot;
	struct tcp_connection_handle owner, found, replacement;
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 0x12345678};
	struct tcp_core_result result;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &owner));
	assert(!tcp_core_connection_create(&table, &tuple, &replacement));
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND); /* reservations are not live matches */
	assert(step_report(&table, owner, &event, &result));
	assert(result.state == TCP_STATE_SYN_SENT);
	assert(!tcp_core_connection_create(&table, &tuple, &replacement));
	assert(tcp_core_connection_get_tuple(&table, owner, &snapshot));
	assert(snapshot.local_port == tuple.local_port &&
	    snapshot.remote_port == tuple.remote_port &&
	    snapshot.local_address[3] == tuple.local_address[3] &&
	    snapshot.remote_address[3] == tuple.remote_address[3]);
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_FOUND);
	assert(found.slot == owner.slot && found.generation == owner.generation);
	/* The lookup reference pins the closed slot until both owners release it. */
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, owner, &event, &result));
	assert(result.state == TCP_STATE_CLOSED);
	assert(tcp_core_connection_get_tuple(&table, found, &snapshot));
	assert(tcp_core_connection_release(&table, owner));
	assert(tcp_core_connection_count(&table) == 1);
	assert(tcp_core_connection_release(&table, found));
	assert(tcp_core_connection_count(&table) == 0);
	query.local_address[3]++;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND);
	query = tuple;
	query.remote_address[3]++;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND);
	query = tuple;
	query.local_port++;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND);
	query = tuple;
	query.remote_port++;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND);
	query = tuple;
	query.remote_port = 0;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_INVALID);
	/* Reuse the slot with another tuple; the released tuple stays absent. */
	tuple = active_tuple();
	tuple.local_port++;
	assert(tcp_core_connection_create(&table, &tuple, &replacement));
	assert(replacement.slot == owner.slot &&
	    replacement.generation != owner.generation);
	assert(!tcp_core_connection_get_tuple(&table, owner, &snapshot));
	assert(tcp_core_connection_get_tuple(&table, replacement, &snapshot));
	assert(snapshot.local_port == tuple.local_port &&
	    snapshot.remote_port == tuple.remote_port &&
	    snapshot.local_address[3] == tuple.local_address[3] &&
	    snapshot.remote_address[3] == tuple.remote_address[3]);
	query = active_tuple();
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND);
	assert(tcp_core_connection_release(&table, replacement));
	puts("TCP tuple lookup: exact four-field match, duplicate rejection, stale tuple and generation-safe reuse PASS");
}

static void
test_listener_lookup_and_conflicts(void)
{
	struct tcp_connection_table table;
	struct tcp_core_tuple listener_tuple = {
		.local_address = {10, 15, 3, 1}, .local_port = 8080
	};
	struct tcp_core_tuple query = {
		.local_address = {10, 15, 3, 1},
		.remote_address = {10, 15, 3, 2},
		.local_port = 8080, .remote_port = 41000
	};
	struct tcp_connection_handle listener, other_listener, found;
	struct tcp_connection_handle active;
	struct tcp_core_event event = {.type = TCP_EVENT_LISTEN};
	struct tcp_core_result result;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &listener_tuple, &listener));
	assert(!tcp_core_connection_create(&table, &listener_tuple,
	    &other_listener));
	{
		struct tcp_core_tuple wildcard = {.local_port = 8080};
		assert(!tcp_core_connection_create(&table, &wildcard,
		    &other_listener)); /* wildcard overlaps specific listener */
	}
	assert(step_report(&table, listener, &event, &result));
	assert(result.state == TCP_STATE_LISTEN);
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_FOUND);
	assert(found.slot == listener.slot &&
	    found.generation == listener.generation);
	assert(tcp_core_connection_release(&table, found));
	/* An exact active tuple takes priority over the matching listener. */
	{
		struct tcp_core_event open = {.type = TCP_EVENT_ACTIVE_OPEN,
		    .initial_sequence = 77};
		assert(tcp_core_connection_create(&table, &query, &active));
		assert(step_report(&table, active, &open, &result));
		assert(tcp_core_connection_lookup(&table, &query, &found) ==
		    TCP_CORE_LOOKUP_FOUND);
		assert(found.slot == active.slot &&
		    found.generation == active.generation);
		assert(tcp_core_connection_release(&table, found));
		event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
		assert(step_report(&table, active, &event, &result));
		assert(tcp_core_connection_release(&table, active));
	}
	query.local_address[3]++;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_NOT_FOUND);
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, listener, &event, &result));
	assert(tcp_core_connection_release(&table, listener));

	tcp_core_table_init(&table);
	listener_tuple = (struct tcp_core_tuple){.local_port = 8080};
	assert(tcp_core_connection_create(&table, &listener_tuple, &listener));
	event = (struct tcp_core_event){.type = TCP_EVENT_LISTEN};
	assert(step_report(&table, listener, &event, &result));
	query.local_address[0] = 192;
	query.local_address[1] = 0;
	query.local_address[2] = 2;
	query.local_address[3] = 99;
	assert(tcp_core_connection_lookup(&table, &query, &found) ==
	    TCP_CORE_LOOKUP_FOUND);
	assert(found.slot == listener.slot);
	assert(tcp_core_connection_release(&table, found));
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, listener, &event, &result));
	assert(tcp_core_connection_release(&table, listener));
	puts("TCP listener lookup: exact and 0.0.0.0 wildcard match; overlapping listener binds rejected PASS");
}

static void
test_payload_is_copied_before_input_release(void)
{
	struct tcp_connection_table table;
	struct tcp_connection_handle handle;
	struct tcp_core_tuple tuple = active_tuple();
	struct tcp_core_event event = {.type = TCP_EVENT_ACTIVE_OPEN,
	    .initial_sequence = 10};
	struct tcp_core_result result;
	struct tcp_segment_view segment;
	uint8_t input[] = {0x31, 0x32, 0x33, 0x34};
	uint8_t output[sizeof(input)];
	size_t output_length;
	tcp_core_table_init(&table);
	assert(tcp_core_connection_create(&table, &tuple, &handle));
	assert(step_report(&table, handle, &event, &result));
	segment = make_view(50, 11, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
	assert(step_report(&table, handle, &(struct tcp_core_event){
	    .type = TCP_EVENT_SEGMENT, .segment = &segment,
	    .source_address = {10, 15, 3, 2},
	    .destination_address = {10, 15, 3, 1}}, &result));
	segment = make_view(51, 11, TCP_FLAG_ACK | TCP_FLAG_PSH,
	    input, sizeof(input));
	assert(step_report(&table, handle, &(struct tcp_core_event){
	    .type = TCP_EVENT_SEGMENT, .segment = &segment,
	    .source_address = {10, 15, 3, 2},
	    .destination_address = {10, 15, 3, 1}}, &result));
	assert(result.accepted_bytes == sizeof(input) &&
	    (result.actions & TCP_ACTION_DELIVER_DATA) != 0);
	for (size_t i = 0; i < sizeof(input); i++)
		input[i] = 0;
	assert(tcp_core_connection_read(&table, handle, output, sizeof(output),
	    &output_length));
	assert(output_length == sizeof(output) && output[0] == 0x31 &&
	    output[3] == 0x34);
	event = (struct tcp_core_event){.type = TCP_EVENT_APPLICATION_CLOSE};
	assert(step_report(&table, handle, &event, &result));
	assert(tcp_core_connection_release(&table, handle));
	puts("TCP action/data ownership: DELIVER_DATA copies payload; source buffer may be released after step PASS");
}

int
main(void)
{
	test_wire();
	test_ipv4_packet_fixtures();
	test_active_state_and_sequence();
	test_isolated_active_passive_handshakes();
	test_passive_close_and_time_wait();
	test_close_wait_last_ack_and_closing();
	test_bounded_send_retransmit_and_window();
	test_receive_bound_and_retry_limit();
	test_time_wait_reset_validation();
	test_table_lifetime_and_backlog();
	test_tcp_service_budget_and_cancellation();
	test_timer_pending_handle_lifetime();
	test_retransmit_timer_wraparound();
	test_listener_backlog_limit();
	test_tuple_lookup_and_duplicate_rules();
	test_listener_lookup_and_conflicts();
	test_payload_is_copied_before_input_release();
	puts("TCP core tests passed; model only, no live TCP enabled");
	return 0;
}
