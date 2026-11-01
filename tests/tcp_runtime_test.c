#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kernel/tcp_runtime.h"

static const uint8_t local_ip[4] = {10, 15, 3, 1};
static const uint8_t peer_ip[4] = {10, 15, 3, 2};

static bool
make_tcp_segment(const uint8_t source[4], const uint8_t destination[4],
	uint16_t source_port, uint16_t destination_port, uint32_t sequence,
	uint32_t acknowledgment, uint8_t segment_flags, const uint8_t *payload,
	size_t payload_length, struct tcp_wire_segment *parsed)
{
	static uint8_t bytes[TCP_WIRE_HEADER_MIN + TCP_WIRE_OPTIONS_MAX + 8];
	struct tcp_wire_spec spec = {
		.source_port = source_port,
		.destination_port = destination_port,
		.sequence = sequence,
		.acknowledgment = acknowledgment,
		.flags = segment_flags,
		.window = 4096,
		.payload = payload,
		.payload_length = payload_length
	};
	struct tcp_wire_pseudo_header pseudo = {0};
	size_t length;
	if (tcp_wire_build(source, destination, &spec, bytes, sizeof(bytes),
	    &length) != TCP_WIRE_OK)
		return false;
	memcpy(pseudo.source, source, 4);
	memcpy(pseudo.destination, destination, 4);
	pseudo.protocol = 6;
	pseudo.segment_length = (uint16_t)length;
	return tcp_wire_parse(&pseudo, bytes, length, parsed) == TCP_WIRE_OK;
}

static bool
build_tcp_bytes(const uint8_t source[4], const uint8_t destination[4],
	uint16_t source_port, uint16_t destination_port, uint32_t sequence,
	uint32_t acknowledgment, uint8_t segment_flags, uint8_t *bytes,
	size_t capacity, size_t *length)
{
	struct tcp_wire_spec spec = {
		.source_port = source_port,
		.destination_port = destination_port,
		.sequence = sequence,
		.acknowledgment = acknowledgment,
		.flags = segment_flags,
		.window = 4096
	};
	return tcp_wire_build(source, destination, &spec, bytes, capacity, length) ==
	    TCP_WIRE_OK;
}

static bool
build_tcp_bytes_window(const uint8_t source[4], const uint8_t destination[4],
	uint16_t source_port, uint16_t destination_port, uint32_t sequence,
	uint32_t acknowledgment, uint8_t segment_flags, uint16_t window,
	const uint8_t *payload, size_t payload_length, uint8_t *bytes,
	size_t capacity, size_t *length)
{
	struct tcp_wire_spec spec = {
		.source_port = source_port,
		.destination_port = destination_port,
		.sequence = sequence,
		.acknowledgment = acknowledgment,
		.flags = segment_flags,
		.window = window,
		.payload = payload,
		.payload_length = payload_length
	};
	return tcp_wire_build(source, destination, &spec, bytes, capacity, length) ==
	    TCP_WIRE_OK;
}

static bool
parse_tcp_bytes(const uint8_t source[4], const uint8_t destination[4],
	const uint8_t *bytes, size_t length, struct tcp_wire_segment *parsed)
{
	struct tcp_wire_pseudo_header pseudo = {0};
	memcpy(pseudo.source, source, 4);
	memcpy(pseudo.destination, destination, 4);
	pseudo.protocol = 6;
	pseudo.segment_length = (uint16_t)length;
	return tcp_wire_parse(&pseudo, bytes, length, parsed) == TCP_WIRE_OK;
}

static bool
make_segment(const uint8_t source[4], const uint8_t destination[4],
	uint16_t source_port, uint16_t destination_port,
	const uint8_t *payload, size_t payload_length,
	struct tcp_wire_segment *parsed)
{
	return make_tcp_segment(source, destination, source_port, destination_port,
	    123, 0, TCP_FLAG_SYN, payload, payload_length, parsed);
}

static struct tcp_core_tuple
tuple_for(uint16_t remote_port)
{
	return (struct tcp_core_tuple){
		.local_address = {10, 15, 3, 1},
		.remote_address = {10, 15, 3, 2},
		.local_port = 42000,
		.remote_port = remote_port
	};
}

static void
assert_route(enum tcp_runtime_route_status expected,
	const uint8_t source[4], const uint8_t destination[4],
	const struct tcp_wire_segment *segment,
	struct tcp_runtime_route_result *route)
{
	enum tcp_runtime_route_status actual = tcp_runtime_route_segment(source,
	    destination, segment, route);
	if (actual != expected)
		printf("route mismatch expected=%u actual=%u srcport=%u dstport=%u\n",
		    expected, actual, segment->source_port,
		    segment->destination_port);
	assert(actual == expected);
	assert(route->status == expected);
}

static void
test_runtime_routing(void)
{
	struct tcp_connection_handle listener, wildcard, active;
	struct tcp_connection_handle extra_listeners[TCP_RUNTIME_CONNECTION_LIMIT];
	struct tcp_connection_handle duplicate = {.slot = 0xa5,
	    .generation = 0xdeadbeef};
	struct tcp_core_tuple active_tuple = tuple_for(443);
	struct tcp_runtime_test_core_result before, after, result;
	struct tcp_wire_segment segment;
	struct tcp_runtime_route_result route;
	uint8_t other_local[4] = {10, 15, 3, 9};
	uint8_t arbitrary_local[4] = {192, 0, 2, 99};
	uint8_t other_peer[4] = {10, 15, 3, 8};
	uint8_t payload = 0x5a;

	assert(tcp_runtime_listener_open(local_ip, 8080, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(tcp_runtime_listener_open(local_ip, 0, &duplicate) ==
	    TCP_RUNTIME_LISTENER_INVALID);
	assert(tcp_runtime_listener_open(local_ip, 8080, &duplicate) ==
	    TCP_RUNTIME_LISTENER_CONFLICT);
	assert(duplicate.slot == 0xa5 && duplicate.generation == 0xdeadbeef);
	assert(make_segment(peer_ip, local_ip, 45000, 8080, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_LISTENER, peer_ip, local_ip, &segment,
	    &route);
	assert(route.has_reference && !route.payload_present &&
	    !route.payload_supported && !route.event_submitted);
	assert(route.handle.slot == listener.slot &&
	    route.handle.generation == listener.generation);
	assert(tcp_runtime_test_snapshot(listener, &before));
	assert(before.state == TCP_STATE_LISTEN && before.actions == 0);
	assert(tcp_runtime_handle_release(route.handle));

	/* Exact local listeners must not match another local address. */
	assert(make_segment(peer_ip, other_local, 45000, 8080, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_NO_MATCH, peer_ip, other_local, &segment,
	    &route);
	assert(!route.has_reference);

	/* An invariant-corruption test alias exercises fail-closed ambiguity. Normal
	 * listener registration rejects this overlapping tuple. */
	assert(tcp_runtime_test_inject_listener_alias(listener, &duplicate));
	assert(make_segment(peer_ip, local_ip, 45000, 8080, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_AMBIGUOUS, peer_ip, local_ip, &segment,
	    &route);
	assert(!route.has_reference);
	assert(tcp_runtime_listener_close(duplicate));
	assert(tcp_runtime_handle_release(duplicate));
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));

	/* Wildcard listeners match a valid local address; conflicting specific
	 * listeners are refused, and repeated SYN classification allocates no child. */
	assert(tcp_runtime_listener_open((uint8_t[4]){0}, 8081, &wildcard) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(tcp_runtime_listener_open(local_ip, 8081, &duplicate) ==
	    TCP_RUNTIME_LISTENER_CONFLICT);
	assert(make_segment(peer_ip, arbitrary_local, 45001, 8081, NULL, 0,
	    &segment));
	assert_route(TCP_RUNTIME_ROUTE_LISTENER, peer_ip, arbitrary_local, &segment,
	    &route);
	assert(route.has_reference && route.handle.slot == wildcard.slot);
	assert(tcp_runtime_handle_release(route.handle));
	assert_route(TCP_RUNTIME_ROUTE_LISTENER, peer_ip, arbitrary_local, &segment,
	    &route); /* duplicate SYN is routing only; same listener, no child */
	assert(route.has_reference && route.handle.slot == wildcard.slot);
	assert(tcp_runtime_handle_release(route.handle));
	assert(tcp_runtime_listener_close(wildcard));
	assert(tcp_runtime_handle_release(wildcard));
	/* If routing had allocated passive children, these bounded slots would not
	 * all be available. Repeated SYN routing alone creates no core entries. */
	for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++)
		assert(tcp_runtime_listener_open(local_ip, (uint16_t)(8100 + i),
		    &extra_listeners[i]) == TCP_RUNTIME_LISTENER_OK);
	assert(tcp_runtime_listener_open(local_ip, 8199, &duplicate) ==
	    TCP_RUNTIME_LISTENER_RESOURCE_EXHAUSTED);
	for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++) {
		assert(tcp_runtime_listener_close(extra_listeners[i]));
		assert(tcp_runtime_handle_release(extra_listeners[i]));
	}

	/* Existing exact entries take precedence over a listener. Each tuple field
	 * is also varied independently to check exact matching. */
	assert(tcp_runtime_test_reserve(&active_tuple, &active));
	duplicate = (struct tcp_connection_handle){
		.slot = 0xa5, .generation = 0xdeadbeef
	};
	assert(!tcp_runtime_test_reserve(&active_tuple, &duplicate));
	assert(duplicate.slot == 0xa5 && duplicate.generation == 0xdeadbeef);
	assert(tcp_runtime_test_activate(active, 0x10203040, &result));
	assert(result.state == TCP_STATE_SYN_SENT &&
	    result.actions == TCP_ACTION_SEND_SYN);
	assert(tcp_runtime_listener_open(local_ip, active_tuple.local_port,
	    &listener) == TCP_RUNTIME_LISTENER_OK);
	assert(make_segment(peer_ip, local_ip, active_tuple.remote_port,
	    active_tuple.local_port, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_CONNECTION, peer_ip, local_ip, &segment,
	    &route);
	assert(route.has_reference && route.handle.slot == active.slot);
	assert(tcp_runtime_handle_release(route.handle));

	assert(tcp_runtime_test_snapshot(active, &before));
	assert(make_segment(other_peer, local_ip, active_tuple.remote_port,
	    active_tuple.local_port, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_LISTENER, other_peer, local_ip, &segment,
	    &route); /* tuple mismatch falls back to the valid listener */
	assert(tcp_runtime_handle_release(route.handle));
	assert(make_segment(peer_ip, other_local, active_tuple.remote_port,
	    active_tuple.local_port, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_NO_MATCH, peer_ip, other_local, &segment,
	    &route); /* local address differs */
	assert(make_segment(peer_ip, local_ip, 444, active_tuple.local_port,
	    NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_LISTENER, peer_ip, local_ip, &segment,
	    &route); /* exact remote port misses; listener is valid fallback */
	assert(tcp_runtime_handle_release(route.handle));
	assert(make_segment(peer_ip, local_ip, active_tuple.remote_port,
	    active_tuple.local_port + 1, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_NO_MATCH, peer_ip, local_ip, &segment,
	    &route); /* local port differs */

	/* Data is reported but unsupported. Routing retains only a handle and does
	 * not submit an event or alter core state. */
	assert(make_segment(peer_ip, local_ip, active_tuple.remote_port,
	    active_tuple.local_port, &payload, 1, &segment));
	assert_route(TCP_RUNTIME_ROUTE_CONNECTION, peer_ip, local_ip, &segment,
	    &route);
	assert(route.has_reference && route.payload_present &&
	    !route.payload_supported && !route.event_submitted);
	assert(tcp_runtime_test_snapshot(active, &after));
	assert(after.state == before.state && after.snd_nxt == before.snd_nxt &&
	    after.rcv_nxt == before.rcv_nxt && after.actions == 0);
	assert(tcp_runtime_handle_release(route.handle));

	/* Lookup reference exhaustion is reported without changing the entry. */
	assert(tcp_runtime_test_set_references(active, UINT16_MAX));
	assert(make_segment(peer_ip, local_ip, active_tuple.remote_port,
	    active_tuple.local_port, NULL, 0, &segment));
	assert_route(TCP_RUNTIME_ROUTE_RESOURCE_EXHAUSTED, peer_ip, local_ip,
	    &segment, &route);
	assert(!route.has_reference);
	assert(tcp_runtime_test_set_references(active, 1));
	assert(!make_segment(peer_ip, local_ip, 0, active_tuple.local_port,
	    NULL, 0, &segment));
	segment.source_port = 0;
	assert_route(TCP_RUNTIME_ROUTE_INVALID, peer_ip, local_ip, &segment,
	    &route);
	assert(make_segment(peer_ip, local_ip, active_tuple.remote_port,
	    active_tuple.local_port, NULL, 0, &segment));
	segment.data_offset = 4;
	assert_route(TCP_RUNTIME_ROUTE_INVALID, peer_ip, local_ip, &segment,
	    &route);
	assert(make_segment(peer_ip, local_ip, active_tuple.remote_port,
	    active_tuple.local_port, NULL, 0, &segment));
	segment.payload_length = 1;
	segment.payload = NULL;
	assert_route(TCP_RUNTIME_ROUTE_INVALID, peer_ip, local_ip, &segment,
	    &route);
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));
	assert(tcp_runtime_test_close(active, 0, &result));
	assert(result.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(active));
	puts("TCP runtime route: exact tuple, listener precedence/wildcard/conflict/ambiguity, bounded handles and unsupported payload PASS");
}

static void
test_runtime_passive_flow(void)
{
	struct tcp_connection_handle listener, children[TCP_CORE_LISTEN_BACKLOG];
	struct tcp_runtime_test_input_result input, duplicate;
	struct tcp_runtime_route_result route;
	struct tcp_wire_segment segment;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_runtime_test_active_result active_input;
	struct tcp_runtime_test_tx_result tx;
	struct tcp_wire_segment serialized;
	uint8_t active_bytes[TCP_WIRE_SEGMENT_MAX];
	uint8_t output[TCP_WIRE_SEGMENT_MAX];
	size_t active_length;
	uint16_t references;
	const uint32_t peer_isn = 0xfffffff0U;
	const uint32_t local_isn = 0xfffffff8U;
	const uint8_t payload = 0x5a;
	uint16_t peer_ports[TCP_CORE_LISTEN_BACKLOG] = {46000, 46001, 46002, 46003};
	assert(tcp_runtime_listener_open(local_ip, 8082, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(make_tcp_segment(peer_ip, local_ip, peer_ports[0], 8082,
	    peer_isn, 0, TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    local_isn, 10, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
	assert(input.has_reference && input.outcome.has_child &&
	    input.outcome.state == TCP_STATE_LISTEN &&
	    input.handle.generation != 0 &&
	    (input.outcome.actions & (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK)) ==
	    (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
	children[0] = input.handle;
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 2); /* caller + passive-child parent link */
	assert(tcp_runtime_test_reference_count(children[0], &references) &&
	    references == 2); /* runtime child link + caller */
	assert(tcp_runtime_test_snapshot(children[0], &snapshot));
	assert(snapshot.state == TCP_STATE_SYN_RECEIVED &&
	    snapshot.snd_una == local_isn && snapshot.snd_nxt == local_isn + 1U &&
	    snapshot.rcv_nxt == peer_isn + 1U && snapshot.timer_armed);
	assert(tcp_runtime_test_next_segment(children[0], output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert((tx.actions & (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK)) ==
	    (TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK));
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &serialized));
	assert(serialized.source_port == 8082 &&
	    serialized.destination_port == peer_ports[0] &&
	    serialized.sequence == local_isn &&
	    serialized.acknowledgment == peer_isn + 1U &&
	    serialized.flags == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
	    serialized.parsed_options.mss_present &&
	    serialized.parsed_options.mss == TCP_CORE_LOCAL_MSS);
	assert(build_tcp_bytes(peer_ip, local_ip, peer_ports[0], 8082, peer_isn, 0,
	    TCP_FLAG_SYN, active_bytes, sizeof(active_bytes), &active_length));
	assert(tcp_runtime_test_active_input(children[0], peer_ip, local_ip,
	    active_bytes, active_length, 10, &active_input) ==
	    TCP_RUNTIME_TEST_ACTIVE_NOT_ACTIVE);
	/* Child tuple routes to the exact child; its caller reference is independent
	 * of the runtime-owned child link. */
	assert(tcp_runtime_route_segment(peer_ip, local_ip, &segment, &route) ==
	    TCP_RUNTIME_ROUTE_CONNECTION);
	assert(route.has_reference && route.handle.slot == children[0].slot &&
	    route.handle.generation == children[0].generation);
	assert(tcp_runtime_handle_release(route.handle));
	assert(tcp_runtime_handle_release(input.handle));
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 2);
	assert(tcp_runtime_test_reference_count(children[0], &references) &&
	    references == 1);
	assert(make_tcp_segment(peer_ip, local_ip, 46000, 8082, peer_isn, 0,
	    TCP_FLAG_SYN, &payload, 1, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    local_isn, 10, &input) == TCP_RUNTIME_TEST_INPUT_UNSUPPORTED);
	assert(make_tcp_segment(peer_ip, local_ip, peer_ports[0], 8082,
	    peer_isn, 0, TCP_FLAG_SYN, NULL, 0, &segment));

	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0x11111111U, 11, &duplicate) ==
	    TCP_RUNTIME_TEST_INPUT_DUPLICATE_SYN);
	assert(duplicate.has_reference && duplicate.handle.slot == children[0].slot &&
	    duplicate.handle.generation == children[0].generation &&
	    duplicate.outcome.state == TCP_STATE_SYN_RECEIVED &&
	    (duplicate.outcome.actions & TCP_ACTION_RETRANSMIT) != 0 &&
	    duplicate.outcome.timer_deadline == snapshot.timer_deadline &&
	    duplicate.outcome.retransmissions == snapshot.retransmissions);
	assert(tcp_runtime_handle_release(duplicate.handle));

	for (size_t i = 1; i < TCP_CORE_LISTEN_BACKLOG; i++) {
		assert(make_tcp_segment(peer_ip, local_ip, peer_ports[i], 8082,
		    (uint32_t)(peer_isn + i), 0, TCP_FLAG_SYN, NULL, 0, &segment));
		assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
		    local_isn + (uint32_t)i, 12 + i, &input) ==
		    TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
		assert(input.has_reference &&
		    input.handle.generation != children[0].generation);
		children[i] = input.handle;
		assert(tcp_runtime_handle_release(input.handle));
	}
	assert(make_tcp_segment(peer_ip, local_ip, 46004, 8082, 1, 0,
	    TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    77, 20, &input) == TCP_RUNTIME_TEST_INPUT_RESOURCE_EXHAUSTED);
	assert(!input.has_reference);
	/* The listener remains usable after children exist. */
	assert(make_tcp_segment(peer_ip, local_ip, 46005, 8082, 2, 0,
	    TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_route_segment(peer_ip, local_ip, &segment, &route) ==
	    TCP_RUNTIME_ROUTE_LISTENER);
	assert(tcp_runtime_handle_release(route.handle));
	assert(!tcp_runtime_listener_close(listener));

	/* Invalid final ACKs leave the child in SYN-RECEIVED. */
	assert(make_tcp_segment(peer_ip, local_ip, peer_ports[0], 8082,
	    peer_isn + 1U, local_isn + 2U, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 21, &input) == TCP_RUNTIME_TEST_INPUT_CORE_REJECTED);
	assert(input.outcome.state == TCP_STATE_SYN_RECEIVED &&
	    input.outcome.actions == TCP_ACTION_SEND_RST && !input.has_reference);
	assert(tcp_runtime_test_snapshot(children[0], &snapshot) &&
	    snapshot.state == TCP_STATE_SYN_RECEIVED);
	assert(make_tcp_segment(peer_ip, local_ip, peer_ports[0], 8082,
	    peer_isn + 2U, local_isn + 1U, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 22, &input) == TCP_RUNTIME_TEST_INPUT_CORE_REJECTED);
	assert(input.outcome.state == TCP_STATE_SYN_RECEIVED &&
	    input.outcome.actions == TCP_ACTION_SEND_ACK && !input.has_reference);
	assert(make_tcp_segment(peer_ip, local_ip, 46099, 8082,
	    peer_isn + 1U, local_isn + 1U, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 23, &input) == TCP_RUNTIME_TEST_INPUT_NO_MATCH);
	assert(make_tcp_segment(peer_ip, local_ip, peer_ports[0], 8082,
	    peer_isn + 1U, local_isn + 1U, TCP_FLAG_ACK | TCP_FLAG_PSH,
	    NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 24, &input) == TCP_RUNTIME_TEST_INPUT_CORE_REJECTED);
	assert(input.outcome.state == TCP_STATE_SYN_RECEIVED &&
	    input.outcome.actions == 0 && !input.has_reference);
	assert(make_tcp_segment(peer_ip, local_ip, peer_ports[0], 8082,
	    peer_isn + 1U, local_isn + 1U, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 25, &input) == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
	assert(input.has_reference && input.outcome.state == TCP_STATE_ESTABLISHED &&
	    input.outcome.actions == 0);
	assert(tcp_runtime_handle_release(input.handle));

	/* A separately retained external handle survives RST cleanup. The runtime
	 * drops its child link, but the core slot remains until that handle releases. */
	assert(tcp_runtime_test_retain(children[0]));
	for (size_t i = 0; i < TCP_CORE_LISTEN_BACKLOG; i++) {
		assert(tcp_runtime_test_snapshot(children[i], &snapshot));
		assert(make_tcp_segment(peer_ip, local_ip, peer_ports[i], 8082,
		    snapshot.rcv_nxt, 0, TCP_FLAG_RST, NULL, 0, &segment));
		assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
		    0, 30 + i, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CLOSED);
		assert(!input.has_reference && input.outcome.state == TCP_STATE_CLOSED);
		assert(tcp_runtime_test_reference_count(listener, &references));
		assert(references == TCP_CORE_LISTEN_BACKLOG - i);
	}
	assert(tcp_runtime_test_snapshot(children[0], &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_handle_release(children[0])); /* explicit retained ref */
	assert(!tcp_runtime_test_snapshot(children[0], &snapshot));
	{
		struct tcp_core_tuple replacement_tuple = tuple_for(9200);
		struct tcp_connection_handle replacement;
		assert(tcp_runtime_test_reserve(&replacement_tuple, &replacement));
		assert(replacement.slot == children[0].slot &&
		    replacement.generation != children[0].generation);
		assert(!tcp_runtime_test_retain(children[0]));
		assert(tcp_runtime_test_release(replacement));
	}
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));
	{
		struct tcp_connection_handle target, fillers[TCP_RUNTIME_CONNECTION_LIMIT - 1];
		assert(tcp_runtime_listener_open(local_ip, 8083, &target) ==
		    TCP_RUNTIME_LISTENER_OK);
		for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT - 1; i++)
			assert(tcp_runtime_listener_open(local_ip,
			    (uint16_t)(8200 + i), &fillers[i]) ==
			    TCP_RUNTIME_LISTENER_OK);
		assert(make_tcp_segment(peer_ip, local_ip, 47000, 8083, 7, 0,
		    TCP_FLAG_SYN, NULL, 0, &segment));
		assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
		    0xabcdef01U, 50, &input) ==
		    TCP_RUNTIME_TEST_INPUT_RESOURCE_EXHAUSTED);
		assert(!input.has_reference);
		for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT - 1; i++) {
			assert(tcp_runtime_listener_close(fillers[i]));
			assert(tcp_runtime_handle_release(fillers[i]));
		}
		assert(tcp_runtime_listener_close(target));
		assert(tcp_runtime_handle_release(target));
	}
	puts("TCP runtime passive open: bounded child/backlog, duplicate SYN, ACK validation, establishment, RST cleanup and retained-reference lifetime PASS");
}

static void
test_runtime_passive_fin_lifecycle(void)
{
	struct tcp_connection_handle listener, child;
	struct tcp_runtime_test_input_result input;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_active_result packet_result;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_wire_segment segment;
	uint8_t wire[TCP_WIRE_SEGMENT_MAX];
	size_t wire_length;
	uint16_t references;
	const uint32_t peer_isn = 1000;
	const uint32_t local_isn = 2000;
	assert(tcp_runtime_listener_open(local_ip, 8085, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(make_tcp_segment(peer_ip, local_ip, 49000, 8085, peer_isn, 0,
	    TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    local_isn, 40, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
	child = input.handle;
	assert(tcp_runtime_handle_release(child));
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 2);

	assert(make_tcp_segment(peer_ip, local_ip, 49000, 8085, peer_isn + 1,
	    local_isn + 1, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 41, &input) == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
	assert(input.outcome.state == TCP_STATE_ESTABLISHED && input.has_reference);
	assert(tcp_runtime_handle_release(input.handle));
	assert(make_tcp_segment(peer_ip, local_ip, 49000, 8085, peer_isn + 1,
	    local_isn + 1, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 42, &input) == TCP_RUNTIME_TEST_INPUT_PROCESSED);
	assert(input.outcome.state == TCP_STATE_CLOSE_WAIT &&
	    (input.outcome.actions & TCP_ACTION_SEND_ACK) != 0);
	assert(tcp_runtime_test_local_close(child, 42, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(close_result.state == TCP_STATE_LAST_ACK &&
	    close_result.timer_armed &&
	    (close_result.actions & (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK)) ==
	    (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK));
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 2); /* relationship remains during LAST-ACK */
	assert(tcp_runtime_test_local_close(child, 43, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(close_result.state == TCP_STATE_LAST_ACK && close_result.actions == 0);
	assert(make_tcp_segment(peer_ip, local_ip, 49000, 8085, peer_isn + 2,
	    close_result.snd_nxt + 1, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 43, &input) == TCP_RUNTIME_TEST_INPUT_CORE_REJECTED);
	assert(tcp_runtime_test_snapshot(child, &snapshot) &&
	    snapshot.state == TCP_STATE_LAST_ACK && snapshot.snd_nxt ==
	    close_result.snd_nxt);
	assert(make_tcp_segment(peer_ip, local_ip, 49000, 8085,
	    peer_isn + 2 + TCP_CORE_RX_CAPACITY,
	    close_result.snd_nxt, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 43, &input) == TCP_RUNTIME_TEST_INPUT_CORE_REJECTED);
	assert(tcp_runtime_test_snapshot(child, &snapshot) &&
	    snapshot.state == TCP_STATE_LAST_ACK);
	assert(build_tcp_bytes(peer_ip, local_ip, 49000, 8085, peer_isn + 2,
	    close_result.snd_nxt, TCP_FLAG_ACK, wire, sizeof(wire), &wire_length));
	assert(tcp_runtime_test_active_input(child, peer_ip, local_ip, wire,
	    wire_length, 43, &packet_result) == TCP_RUNTIME_TEST_ACTIVE_NOT_ACTIVE);
	/* Active-input intentionally refuses passive handles; use the shared
	 * passive segment path to deliver the LAST-ACK acknowledgment. */
	assert(make_tcp_segment(peer_ip, local_ip, 49000, 8085, peer_isn + 2,
	    close_result.snd_nxt, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 43, &input) == TCP_RUNTIME_TEST_INPUT_PROCESSED);
	assert(input.outcome.state == TCP_STATE_CLOSED);
	assert(!tcp_runtime_test_snapshot(child, &snapshot));
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 1);
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));
	puts("TCP runtime passive FIN lifecycle: ESTABLISHED -> CLOSE-WAIT -> LAST-ACK -> CLOSED; parent unlink and references PASS");
}

static void
test_runtime_passive_last_ack_reset_and_reuse(void)
{
	struct tcp_connection_handle listener, child, replacement_listener;
	struct tcp_runtime_test_input_result input;
	struct tcp_runtime_test_active_result active;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_wire_segment segment;
	uint16_t references;
	const uint16_t port = 8087;
	const uint32_t peer_isn = 0x20000;
	const uint32_t local_isn = 0x10000;
	struct tcp_core_tuple tuple = tuple_for(8088);
	assert(tcp_runtime_listener_open(local_ip, port, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(make_tcp_segment(peer_ip, local_ip, 49100, port, peer_isn, 0,
	    TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    local_isn, 60, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
	child = input.handle;
	assert(input.outcome.state == TCP_STATE_LISTEN && input.outcome.has_child);
	assert(make_tcp_segment(peer_ip, local_ip, 49100, port, peer_isn + 1,
	    local_isn + 1, TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 61, &input) == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
	assert(input.outcome.state == TCP_STATE_ESTABLISHED &&
	    input.outcome.actions == 0);
	assert(tcp_runtime_handle_release(input.handle));
	assert(make_tcp_segment(peer_ip, local_ip, 49100, port, peer_isn + 1,
	    local_isn + 1, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 62, &input) == TCP_RUNTIME_TEST_INPUT_PROCESSED);
	assert(input.outcome.state == TCP_STATE_CLOSE_WAIT &&
	    input.outcome.actions == TCP_ACTION_SEND_ACK);
	assert(tcp_runtime_test_local_close(child, 63, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(close_result.state == TCP_STATE_LAST_ACK &&
	    close_result.actions == (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK));
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 2);
	assert(!tcp_runtime_listener_close(listener));
	/* RST in LAST-ACK follows Core's in-window reset path. Runtime removes the
	 * relationship exactly once while the caller's child reference survives. */
	assert(make_tcp_segment(peer_ip, local_ip, 49100, port, peer_isn + 2,
	    0, TCP_FLAG_RST, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 64, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CLOSED);
	assert(input.outcome.state == TCP_STATE_CLOSED &&
	    input.outcome.actions == TCP_ACTION_ABORT);
	assert(tcp_runtime_test_reference_count(listener, &references) &&
	    references == 1);
	assert(tcp_runtime_test_snapshot(child, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));
	assert(tcp_runtime_test_release(child));
	assert(!tcp_runtime_test_snapshot(child, &snapshot));

	/* Keep slot zero occupied so active-open reuses the old child slot. The
	 * stale child generation cannot release or affect the replacement. */
	assert(tcp_runtime_listener_open(local_ip, 8089, &replacement_listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(tcp_runtime_test_active_open(&tuple, 0x30000, 65, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(active.handle.slot == child.slot &&
	    active.handle.generation != child.generation);
	assert(!tcp_runtime_test_release(child));
	assert(tcp_runtime_test_close(active.handle, 66, &snapshot));
	assert(snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(active.handle));
	assert(tcp_runtime_listener_close(replacement_listener));
	assert(tcp_runtime_handle_release(replacement_listener));
	puts("TCP runtime passive close: LAST-ACK RST, one-time parent unlink, retained child, stale generation and slot reuse PASS");
}

static void
test_runtime_active_open(void)
{
	struct tcp_core_tuple tuple = tuple_for(443);
	struct tcp_runtime_test_active_result active, input;
	struct tcp_runtime_timer_result timers;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_connection_handle handle;
	struct tcp_wire_spec spec = {0};
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
	uint8_t other_ip[4] = {10, 15, 3, 9};
	size_t length;
	const uint32_t local_isn = 0xfffffffeU;
	const uint32_t peer_isn = 0x12345678U;

	{
		struct tcp_core_tuple invalid = tuple;
		invalid.local_address[0] = 0;
		invalid.local_address[1] = 0;
		invalid.local_address[2] = 0;
		invalid.local_address[3] = 0;
		assert(tcp_runtime_test_active_open(&invalid, local_isn, 10,
		    &active) == TCP_RUNTIME_TEST_ACTIVE_INVALID);
		invalid = tuple;
		invalid.remote_port = 0;
		assert(tcp_runtime_test_active_open(&invalid, local_isn, 10,
		    &active) == TCP_RUNTIME_TEST_ACTIVE_INVALID);
	}
	assert(tcp_runtime_test_active_open(&tuple, local_isn, 10, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(active.has_reference && active.state == TCP_STATE_SYN_SENT &&
	    active.actions == TCP_ACTION_SEND_SYN && active.snd_una == local_isn &&
	    active.snd_nxt == 0xffffffffU && active.timer_armed &&
	    active.timer_deadline == 110);
	handle = active.handle;
	assert(tcp_runtime_test_active_open(&tuple, 1, 10, &input) ==
	    TCP_RUNTIME_TEST_ACTIVE_DUPLICATE);
	assert(!input.has_reference);

	/* Before SYN-ACK, a valid but wrong ACK and ACK without SYN leave the core
	 * in SYN-SENT. The first SYN-ACK's sequence is the peer's ISN; no earlier
	 * peer sequence exists against which to reject an arbitrary value. */
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 1, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 11, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_SYN_SENT &&
	    (input.actions & TCP_ACTION_SEND_RST) != 0);
	assert(tcp_runtime_test_snapshot(handle, &snapshot));
	assert(snapshot.state == TCP_STATE_SYN_SENT && snapshot.snd_una == local_isn);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 9U, 0xffffffffU, TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 12, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_SYN_SENT);

	/* Each tuple component is matched exactly before the core receives an event. */
	assert(build_tcp_bytes(other_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 0xffffffffU, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, other_ip, local_ip, bytes,
	    length, 13, &input) == TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH);
	assert(build_tcp_bytes(peer_ip, other_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 0xffffffffU, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, other_ip, bytes,
	    length, 13, &input) == TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port + 1,
	    tuple.local_port, peer_isn, 0xffffffffU, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 13, &input) == TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port + 1, peer_isn, 0xffffffffU,
	    TCP_FLAG_SYN | TCP_FLAG_ACK, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 13, &input) == TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH);

	/* Invalid flag combinations do not establish; malformed and checksum-bad
	 * wire data is rejected before tcp_core_step(). */
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 0xffffffffU,
	    TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_FIN, bytes, sizeof(bytes),
	    &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 14, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_SYN_SENT);
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length - 1, 14, &input) == TCP_RUNTIME_TEST_ACTIVE_MALFORMED);
	bytes[length - 1] ^= 1;
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 14, &input) == TCP_RUNTIME_TEST_ACTIVE_MALFORMED);
	bytes[length - 1] ^= 1;
	spec = (struct tcp_wire_spec){
		.source_port = tuple.remote_port,
		.destination_port = tuple.local_port,
		.sequence = peer_isn,
		.acknowledgment = 0xffffffffU,
		.flags = TCP_FLAG_SYN | TCP_FLAG_ACK | TCP_FLAG_ECE,
		.window = 4096
	};
	assert(tcp_wire_build(peer_ip, local_ip, &spec, bytes, sizeof(bytes),
	    &length) == TCP_WIRE_OK);
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 14, &input) == TCP_RUNTIME_TEST_ACTIVE_CORE_ERROR);
	assert(tcp_runtime_test_snapshot(handle, &snapshot) &&
	    snapshot.state == TCP_STATE_SYN_SENT);

	/* A correct SYN-ACK is the only event here that establishes the connection;
	 * Core validates its ACK and returns a final ACK action, never transmitted. */
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 0xffffffffU, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 15, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_ESTABLISHED &&
	    input.actions == TCP_ACTION_SEND_ACK && input.snd_una == 0xffffffffU &&
	    input.rcv_nxt == peer_isn + 1U);
	/* A duplicate SYN-ACK after establishment cannot repeat the handshake or
	 * allocate another connection. Core remains authoritative for its outcome. */
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 16, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_ESTABLISHED &&
	    (input.actions & TCP_ACTION_SEND_SYN) == 0);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1U, 0, TCP_FLAG_RST, bytes,
	    sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 17, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_CLOSED &&
	    (input.actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_runtime_test_release(handle));

	/* RST requires the Core's exact acknowledgment in SYN-SENT. */
	tuple.remote_port = 444;
	assert(tcp_runtime_test_active_open(&tuple, 0x1000, 20, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	handle = active.handle;
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, 0x5555, 0, TCP_FLAG_RST, bytes, sizeof(bytes),
	    &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 21, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_SYN_SENT && input.actions == 0);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, 0x5555, 0x1002, TCP_FLAG_RST | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 22, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_SYN_SENT);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, 0x5555, 0x1001, TCP_FLAG_RST | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 23, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_CLOSED &&
	    (input.actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_runtime_test_release(handle));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 24, &input) == TCP_RUNTIME_TEST_ACTIVE_STALE_HANDLE);

	/* A SYN-SENT timeout is serviced through the runtime owner, remains bounded,
	 * and does not invalidate the external handle before it is released. */
	tuple.remote_port = 445;
	assert(tcp_runtime_test_active_open(&tuple, 0x2000, 1000, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	handle = active.handle;
	assert(tcp_runtime_service_timers(1100, 1, &timers) ==
	    TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && timers.items[0].state == TCP_STATE_SYN_SENT &&
	    (timers.items[0].actions & TCP_ACTION_RETRANSMIT) != 0);
	assert(tcp_runtime_test_snapshot(handle, &snapshot) &&
	    snapshot.state == TCP_STATE_SYN_SENT);
	const uint64_t deadlines[] = {1300, 1700, 2500, 3300, 4100};
	for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++)
		assert(tcp_runtime_service_timers(deadlines[i], 1, &timers) ==
		    TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && timers.items[0].state == TCP_STATE_CLOSED &&
	    (timers.items[0].actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_runtime_test_snapshot(handle, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(handle));
	assert(!tcp_runtime_test_snapshot(handle, &snapshot));
	{
		struct tcp_connection_handle replacement;
		assert(tcp_runtime_test_active_open(&tuple, 0x3000, 5000,
		    &active) == TCP_RUNTIME_TEST_ACTIVE_OK);
		replacement = active.handle;
		assert(replacement.slot == handle.slot &&
		    replacement.generation != handle.generation);
		assert(!tcp_runtime_test_snapshot(handle, &snapshot));
		assert(tcp_runtime_test_close(replacement, 5000, &snapshot));
		assert(tcp_runtime_test_release(replacement));
	}

	/* Active-open capacity shares the fixed eight-slot runtime table and a full
	 * table rejects atomically without exposing a reference. */
	{
		struct tcp_connection_handle entries[TCP_RUNTIME_CONNECTION_LIMIT];
		struct tcp_core_tuple candidate = tuple;
		for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++) {
			candidate.remote_port = (uint16_t)(5000 + i);
			assert(tcp_runtime_test_active_open(&candidate,
			    (uint32_t)(0x4000 + i), 6000, &active) ==
			    TCP_RUNTIME_TEST_ACTIVE_OK);
			entries[i] = active.handle;
		}
		candidate.remote_port = 6000;
		assert(tcp_runtime_test_active_open(&candidate, 1, 6000, &input) ==
		    TCP_RUNTIME_TEST_ACTIVE_RESOURCE_EXHAUSTED);
		assert(!input.has_reference);
		for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++) {
			assert(tcp_runtime_test_close(entries[i], 6000, &snapshot));
			assert(snapshot.state == TCP_STATE_CLOSED);
			assert(tcp_runtime_test_release(entries[i]));
		}
	}
	puts("TCP runtime active open: deterministic SYN-SENT, exact tuple, Core SYN-ACK/RST validation, timeout ownership and bounded cleanup PASS");
}

static void
test_runtime_active_fin_and_time_wait(void)
{
	struct tcp_core_tuple tuple = tuple_for(447);
	struct tcp_runtime_test_active_result active, packet;
	struct tcp_runtime_timer_result timers;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_connection_handle handle;
	struct tcp_runtime_test_close_result close_result;
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
	size_t length;
	const uint32_t local_isn = 3000;
	const uint32_t peer_isn = 4000;
	assert(tcp_runtime_test_active_open(&tuple, local_isn, 100, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	handle = active.handle;
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, local_isn + 1, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 101, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_ESTABLISHED);
	assert(tcp_runtime_test_local_close(handle, 110, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(close_result.state == TCP_STATE_FIN_WAIT_1 && close_result.snd_nxt ==
	    local_isn + 2 && close_result.timer_deadline == 210);
	assert(close_result.actions == (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK));
	assert(tcp_runtime_test_local_close(handle, 110, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(close_result.state == TCP_STATE_FIN_WAIT_1 && close_result.actions == 0);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, local_isn + 3, TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 110, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_FIN_WAIT_1 &&
	    (packet.actions & TCP_ACTION_SEND_ACK) != 0);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, local_isn + 2, TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 111, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_FIN_WAIT_2);
	assert(tcp_runtime_test_local_close(handle, 111, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(close_result.state == TCP_STATE_FIN_WAIT_2 && close_result.actions == 0);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, local_isn + 2,
	    TCP_FLAG_FIN | TCP_FLAG_ACK, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 120, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_TIME_WAIT &&
	    (packet.actions & TCP_ACTION_ENTER_TIME_WAIT) != 0);
	assert(tcp_runtime_test_local_close(handle, 120, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	/* A duplicate FIN is ACKed and restarts TIME-WAIT in Core. */
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 200, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_TIME_WAIT &&
	    packet.actions == TCP_ACTION_SEND_ACK);
	assert(tcp_runtime_service_timers(120 + TCP_CORE_TIME_WAIT_TICKS, 1,
	    &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 0 && !timers.more_due);
	assert(tcp_runtime_service_timers(200 + TCP_CORE_TIME_WAIT_TICKS, 1,
	    &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && timers.items[0].state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_snapshot(handle, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_local_close(handle,
	    200 + TCP_CORE_TIME_WAIT_TICKS, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(close_result.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(handle));
	assert(!tcp_runtime_test_snapshot(handle, &snapshot));

	/* RST during FIN-WAIT-1 and timeout during FIN-WAIT-1 both use the same
	 * terminal reclamation rule, while caller references remain releasable. */
	tuple.remote_port = 448;
	assert(tcp_runtime_test_active_open(&tuple, 5000, 300, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	handle = active.handle;
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 5001, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 301, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(tcp_runtime_test_close(handle, 302, &snapshot));
	assert(snapshot.state == TCP_STATE_FIN_WAIT_1);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, 0, TCP_FLAG_RST, bytes, sizeof(bytes),
	    &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 303, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(handle));

	tuple.remote_port = 449;
	assert(tcp_runtime_test_active_open(&tuple, 6000, 500, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	handle = active.handle;
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, 6001, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(handle, peer_ip, local_ip, bytes,
	    length, 501, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(tcp_runtime_test_close(handle, 502, &snapshot));
	assert(snapshot.state == TCP_STATE_FIN_WAIT_1 &&
	    snapshot.timer_deadline == 602);
	{
		const uint64_t deadlines[] = {602, 802, 1202, 2002, 2802, 3602};
		for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++)
			assert(tcp_runtime_service_timers(deadlines[i], 1, &timers) ==
			    TCP_RUNTIME_TIMER_OK);
	}
	assert(timers.count == 1 && timers.items[0].state == TCP_STATE_CLOSED &&
	    (timers.items[0].actions & TCP_ACTION_ABORT) != 0);
	assert(tcp_runtime_test_snapshot(handle, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(handle));
	puts("TCP runtime active close: FIN-WAIT states, RST/timeout cleanup, TIME-WAIT restart/expiry and handle lifetime PASS");
}

static void
test_runtime_rst_in_fin_wait_2_and_closing(void)
{
	struct tcp_core_tuple tuple = tuple_for(452);
	struct tcp_runtime_test_active_result active, packet;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_core_result snapshot;
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
	size_t length;
	const uint32_t local_isn = 0x5000;
	const uint32_t peer_isn = 0x6000;

	/* RST in FIN-WAIT-2 is accepted at the exact next peer sequence and closes
	 * Core table ownership; the external Runtime handle remains releasable. */
	assert(tcp_runtime_test_active_open(&tuple, local_isn, 70, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port, tuple.local_port,
	    peer_isn, local_isn + 1, TCP_FLAG_SYN | TCP_FLAG_ACK, bytes,
	    sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 71, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_ESTABLISHED);
	assert(tcp_runtime_test_local_close(active.handle, 72, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port, tuple.local_port,
	    peer_isn + 1, local_isn + 2, TCP_FLAG_ACK, bytes, sizeof(bytes),
	    &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 73, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_FIN_WAIT_2);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port, tuple.local_port,
	    peer_isn + 1, 0, TCP_FLAG_RST, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 74, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_CLOSED &&
	    packet.actions == TCP_ACTION_ABORT);
	assert(tcp_runtime_test_snapshot(active.handle, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(active.handle));

	/* A peer FIN before our FIN acknowledgment enters CLOSING. An in-window
	 * reset there follows the same Core terminal path, without a second FIN. */
	tuple.remote_port++;
	assert(tcp_runtime_test_active_open(&tuple, local_isn + 10, 80, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port, tuple.local_port,
	    peer_isn + 10, local_isn + 11, TCP_FLAG_SYN | TCP_FLAG_ACK, bytes,
	    sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 81, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(tcp_runtime_test_local_close(active.handle, 82, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port, tuple.local_port,
	    peer_isn + 11, 0, TCP_FLAG_FIN, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 83, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_CLOSING);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port, tuple.local_port,
	    peer_isn + 12, 0, TCP_FLAG_RST, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 84, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_CLOSED &&
	    packet.actions == TCP_ACTION_ABORT);
	assert(tcp_runtime_test_release(active.handle));
	puts("TCP runtime RST close states: FIN-WAIT-2 and CLOSING exact-sequence reset and retained-handle cleanup PASS");
}

static void
test_runtime_mixed_capacity(void)
{
	struct tcp_connection_handle listener, child, active[TCP_RUNTIME_CONNECTION_LIMIT];
	struct tcp_connection_handle replacement, rejected = {
		.slot = 0xa5, .generation = 0xdeadbeef
	};
	struct tcp_runtime_test_input_result input;
	struct tcp_runtime_test_active_result result;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_wire_segment segment;
	struct tcp_core_tuple tuple;
	uint16_t refs;

	/* One listener and one passive child consume the same eight slots used by
	 * active connections. The child's caller reference is released here; its
	 * runtime relationship remains the sole child reference until terminal
	 * cleanup. */
	assert(tcp_runtime_listener_open(local_ip, 8085, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(make_tcp_segment(peer_ip, local_ip, 48100, 8085, 91, 0,
	    TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0x12340000, 10, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
	child = input.handle;
	assert(tcp_runtime_test_release(child));
	assert(tcp_runtime_test_reference_count(listener, &refs) && refs == 2);
	assert(tcp_runtime_test_reference_count(child, &refs) && refs == 1);

	for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT - 2; i++) {
		tuple = tuple_for((uint16_t)(9500 + i));
		assert(tcp_runtime_test_active_open(&tuple, (uint32_t)(0x2000 + i),
		    20, &result) == TCP_RUNTIME_TEST_ACTIVE_OK);
		active[i] = result.handle;
	}
	tuple = tuple_for(9600);
	assert(tcp_runtime_test_active_open(&tuple, 1, 20, &result) ==
	    TCP_RUNTIME_TEST_ACTIVE_RESOURCE_EXHAUSTED);
	assert(!result.has_reference);
	assert(tcp_runtime_listener_open(local_ip, 8086, &rejected) ==
	    TCP_RUNTIME_LISTENER_RESOURCE_EXHAUSTED);
	assert(rejected.slot == 0xa5 && rejected.generation == 0xdeadbeef);

	/* Releasing one active slot permits allocation and advances its generation. */
	assert(tcp_runtime_test_close(active[2], 30, &snapshot));
	assert(snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(active[2]));
	tuple = tuple_for(9601);
	assert(tcp_runtime_test_active_open(&tuple, 0x3000, 31, &result) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	replacement = result.handle;
	assert(replacement.slot == active[2].slot &&
	    replacement.generation != active[2].generation);
	assert(!tcp_runtime_test_snapshot(active[2], &snapshot));
	assert(tcp_runtime_test_local_close(active[2], 32, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_STALE_HANDLE);
	assert(tcp_runtime_test_close(replacement, 32, &snapshot));
	assert(tcp_runtime_test_release(replacement));

	for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT - 2; i++) {
		if (i == 2)
			continue;
		assert(tcp_runtime_test_close(active[i], 40, &snapshot));
		assert(snapshot.state == TCP_STATE_CLOSED);
		assert(tcp_runtime_test_release(active[i]));
	}

	/* A reset terminally closes exactly this child, removes its parent link,
	 * and leaves the listener independently closable. */
	assert(tcp_runtime_test_snapshot(child, &snapshot));
	assert(make_tcp_segment(peer_ip, local_ip, 48100, 8085,
	    snapshot.rcv_nxt, 0, TCP_FLAG_RST, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    0, 51, &input) == TCP_RUNTIME_TEST_INPUT_CHILD_CLOSED);
	assert(tcp_runtime_test_reference_count(listener, &refs) && refs == 1);
	assert(!tcp_runtime_test_snapshot(child, &snapshot));
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));
	puts("TCP runtime mixed lifecycle: shared eight-slot active/passive capacity, atomic exhaustion, generation reuse and parent unlink PASS");
}

static void
test_runtime_local_close_state_policy(void)
{
	struct tcp_runtime_test_close_result closed;
	struct tcp_runtime_test_active_result active, packet;
	struct tcp_runtime_test_input_result passive;
	struct tcp_runtime_timer_result timers;
	struct tcp_runtime_test_core_result snapshot;
	struct tcp_connection_handle listener;
	struct tcp_wire_segment segment;
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
	size_t length;
	uint32_t local_isn = 7000, peer_isn = 8000;
	struct tcp_core_tuple tuple = tuple_for(9711);

	/* LISTEN has a dedicated close operation so the child-link check cannot be
	 * bypassed by the generic local-close test seam. */
	assert(tcp_runtime_listener_open(local_ip, 8090, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(tcp_runtime_test_local_close(listener, 1, &closed) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(closed.state == TCP_STATE_LISTEN && closed.actions == 0);
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));

	/* A Core-defined close in SYN-RECEIVED aborts with an abstract RST and
	 * unlinks the child; it does not emit a FIN. */
	assert(tcp_runtime_listener_open(local_ip, 8091, &listener) ==
	    TCP_RUNTIME_LISTENER_OK);
	assert(make_tcp_segment(peer_ip, local_ip, 48110, 8091, peer_isn, 0,
	    TCP_FLAG_SYN, NULL, 0, &segment));
	assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &segment,
	    local_isn, 2, &passive) == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
	assert(tcp_runtime_test_local_close(passive.handle, 3, &closed) ==
	    TCP_RUNTIME_TEST_CLOSE_HANDSHAKE_ABORTED);
	assert(closed.state == TCP_STATE_CLOSED &&
	    closed.actions == (TCP_ACTION_SEND_RST | TCP_ACTION_ABORT));
	assert(tcp_runtime_handle_release(passive.handle));
	assert(tcp_runtime_listener_close(listener));
	assert(tcp_runtime_handle_release(listener));

	/* Simultaneous close enters CLOSING. A second local close is rejected
	 * without altering sequence state or generating a second FIN. */
	assert(tcp_runtime_test_active_open(&tuple, local_isn, 10, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, local_isn + 1, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 11, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_ESTABLISHED);
	assert(tcp_runtime_test_local_close(active.handle, 12, &closed) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, 0, TCP_FLAG_FIN, bytes, sizeof(bytes),
	    &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 13, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_CLOSING);
	assert(tcp_runtime_test_local_close(active.handle, 14, &closed) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(closed.state == TCP_STATE_CLOSING && closed.actions == 0 &&
	    closed.snd_nxt == local_isn + 2);
	assert(build_tcp_bytes(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 2, local_isn + 2, TCP_FLAG_ACK, bytes,
	    sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 15, &packet) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(packet.state == TCP_STATE_TIME_WAIT);
	assert(tcp_runtime_service_timers(15 + TCP_CORE_TIME_WAIT_TICKS, 1,
	    &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && timers.items[0].state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_snapshot(active.handle, &snapshot) &&
	    snapshot.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_local_close(active.handle,
	    15 + TCP_CORE_TIME_WAIT_TICKS, &closed) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(closed.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_release(active.handle));
	puts("TCP runtime local-close policy: LISTEN separation, SYN-RECEIVED abort, CLOSING rejection and TIME-WAIT terminal cleanup PASS");
}

static void
test_runtime_stream_transport(void)
{
	struct tcp_core_tuple tuple = tuple_for(9820);
	struct tcp_runtime_test_active_result active, input;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_io_result io;
	struct tcp_runtime_test_tx_result tx;
	struct tcp_runtime_timer_result timers;
	struct tcp_wire_segment parsed;
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
	uint8_t output[TCP_WIRE_SEGMENT_MAX];
	uint8_t readback[8];
	uint8_t overflow[TCP_CORE_TX_CAPACITY];
	uint8_t rxfill[TCP_CORE_RX_CAPACITY];
	size_t length;
	const uint32_t local_isn = 0x1000;
	const uint32_t peer_isn = 0x2000;
	uint8_t application[] = "abcdefgh";
	static const uint8_t peer_data[] = "peer";

	assert(tcp_runtime_test_active_open(&tuple, local_isn, 0, &active) ==
	    TCP_RUNTIME_TEST_ACTIVE_OK);
	/* A short output buffer does not consume the queued SYN. */
	assert(tcp_runtime_test_next_segment(active.handle, output, 20, &tx) ==
	    TCP_RUNTIME_TEST_TX_OUTPUT_TOO_SMALL);
	assert(tx.length == 0);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(tx.length == 24 && (tx.actions & TCP_ACTION_SEND_SYN) != 0);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.source_port == tuple.local_port &&
	    parsed.destination_port == tuple.remote_port &&
	    parsed.sequence == local_isn && parsed.flags == TCP_FLAG_SYN &&
	    parsed.parsed_options.mss_present &&
	    parsed.parsed_options.mss == TCP_CORE_LOCAL_MSS);

	assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn, local_isn + 1, TCP_FLAG_SYN | TCP_FLAG_ACK,
	    4, NULL, 0, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 1, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_ESTABLISHED &&
	    input.actions == TCP_ACTION_SEND_ACK);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.flags == TCP_FLAG_ACK && parsed.sequence == local_isn + 1 &&
	    parsed.acknowledgment == peer_isn + 1 && parsed.payload_length == 0);

	assert(tcp_runtime_test_write(active.handle, NULL, 0, 2, &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 0);
	assert(tcp_runtime_test_write((struct tcp_connection_handle){
	    .slot = active.handle.slot, .generation = active.handle.generation + 1},
	    NULL, 0, 2, &io) == TCP_RUNTIME_TEST_IO_STALE_HANDLE);
	assert(tcp_runtime_test_write(active.handle, application, 4, 2, &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 4);
	assert(tcp_runtime_test_write(active.handle, application + 4, 4, 2, &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 4);
	memset(overflow, 0xa5, sizeof(overflow));
	assert(tcp_runtime_test_write(active.handle, overflow, sizeof(overflow),
	    2, &io) == TCP_RUNTIME_TEST_IO_FULL && io.bytes == 0);
	assert(tcp_runtime_test_local_close(active.handle, 2, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_BUSY);
	/* Caller memory can be changed immediately; Core owns its queued copy. */
	memset(application, 0, sizeof(application));
	assert(tcp_runtime_test_next_segment(active.handle, output,
	    TCP_WIRE_HEADER_MIN, &tx) ==
	    TCP_RUNTIME_TEST_TX_OUTPUT_TOO_SMALL);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.sequence == local_isn + 1 &&
	    parsed.acknowledgment == peer_isn + 1 && parsed.payload_length == 4 &&
	    memcmp(parsed.payload, "abcd", 4) == 0);

	/* ACK of the first bounded segment opens the next one. Its ACK is dropped;
	 * the deterministic Runtime timer produces an identical serialized retry. */
	assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, local_isn + 5, TCP_FLAG_ACK, 4, NULL, 0,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 2, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert((input.actions & TCP_ACTION_SEND_DATA) != 0);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.sequence == local_isn + 5 && parsed.payload_length == 4 &&
	    memcmp(parsed.payload, "efgh", 4) == 0);
	assert(tcp_runtime_service_timers(102, 1, &timers) ==
	    TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 &&
	    (timers.items[0].actions & TCP_ACTION_SEND_DATA) != 0 &&
	    (timers.items[0].actions & TCP_ACTION_RETRANSMIT) != 0);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert((tx.actions & TCP_ACTION_RETRANSMIT) != 0 &&
	    parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.sequence == local_isn + 5 && parsed.payload_length == 4 &&
	    memcmp(parsed.payload, "efgh", 4) == 0);

	assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, local_isn + 9, TCP_FLAG_ACK, 4, NULL, 0,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 103, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 1, local_isn + 9,
	    TCP_FLAG_ACK | TCP_FLAG_PSH, 4, peer_data, sizeof(peer_data) - 1,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 104, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.accepted_bytes == sizeof(peer_data) - 1 &&
	    (input.actions & TCP_ACTION_DELIVER_DATA) != 0);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.flags == TCP_FLAG_ACK && parsed.sequence == local_isn + 9 &&
	    parsed.acknowledgment == peer_isn + 5);
	assert(tcp_runtime_test_read(active.handle, readback, 0, &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 0);
	assert(tcp_runtime_test_read(active.handle, readback, 2, &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 2 && memcmp(readback, "pe", 2) == 0);
	assert(tcp_runtime_test_read(active.handle, readback, sizeof(readback), &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 2 && memcmp(readback, "er", 2) == 0);
	assert(tcp_runtime_test_read(active.handle, readback, sizeof(readback), &io) ==
	    TCP_RUNTIME_TEST_IO_WOULD_BLOCK);
	/* Fill the bounded receive ring. Reading from a zero-window state must
	 * queue a window-update ACK with the newly available capacity. */
	memset(rxfill, 0x6d, sizeof(rxfill));
	for (size_t offset = 0; offset < sizeof(rxfill); offset += 1024) {
		assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
		    tuple.local_port, peer_isn + 5 + (uint32_t)offset,
		    local_isn + 9, TCP_FLAG_ACK | TCP_FLAG_PSH, 4,
		    rxfill + offset, 1024, bytes, sizeof(bytes), &length));
		assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip,
		    bytes, length, 104 + offset / 1024, &input) ==
		    TCP_RUNTIME_TEST_ACTIVE_OK);
		assert(input.accepted_bytes == 1024 &&
		    input.receive_window == (offset == 0 ? 1024 : 0));
		assert(tcp_runtime_test_next_segment(active.handle, output,
		    sizeof(output), &tx) == TCP_RUNTIME_TEST_TX_OK);
		assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
		assert(parsed.flags == TCP_FLAG_ACK &&
		    parsed.acknowledgment == peer_isn + 5 + (uint32_t)offset + 1024 &&
		    parsed.window == input.receive_window);
	}
	assert(tcp_runtime_test_read(active.handle, rxfill, 512, &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 512);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed) &&
	    parsed.flags == TCP_FLAG_ACK && parsed.acknowledgment == peer_isn + 2053 &&
	    parsed.window == 512);
	assert(tcp_runtime_test_read(active.handle, rxfill, sizeof(rxfill), &io) ==
	    TCP_RUNTIME_TEST_IO_OK && io.bytes == 1536);

	assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 2053, local_isn + 9,
	    TCP_FLAG_FIN | TCP_FLAG_ACK, 4, NULL, 0, bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 105, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_CLOSE_WAIT &&
	    input.rcv_nxt == peer_isn + 2054);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed) &&
	    parsed.acknowledgment == peer_isn + 2054 && parsed.flags == TCP_FLAG_ACK);
	assert(tcp_runtime_test_read(active.handle, readback, sizeof(readback), &io) ==
	    TCP_RUNTIME_TEST_IO_EOF);
	assert(tcp_runtime_test_local_close(active.handle, 106, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(close_result.state == TCP_STATE_LAST_ACK);
	assert(tcp_runtime_test_next_segment(active.handle, output, sizeof(output),
	    &tx) == TCP_RUNTIME_TEST_TX_OK);
	assert(parse_tcp_bytes(local_ip, peer_ip, output, tx.length, &parsed));
	assert(parsed.flags == (TCP_FLAG_FIN | TCP_FLAG_ACK) &&
	    parsed.sequence == local_isn + 9 &&
	    parsed.acknowledgment == peer_isn + 2054);
	assert(build_tcp_bytes_window(peer_ip, local_ip, tuple.remote_port,
	    tuple.local_port, peer_isn + 2054, local_isn + 10, TCP_FLAG_ACK, 4, NULL, 0,
	    bytes, sizeof(bytes), &length));
	assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip, bytes,
	    length, 107, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
	assert(input.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_read(active.handle, readback, sizeof(readback), &io) ==
	    TCP_RUNTIME_TEST_IO_EOF);
	assert(tcp_runtime_test_release(active.handle));
	{
		struct tcp_core_tuple reset_tuple = tuple_for(9821);
		assert(tcp_runtime_test_active_open(&reset_tuple, 0x3000, 200,
		    &active) == TCP_RUNTIME_TEST_ACTIVE_OK);
		assert(build_tcp_bytes(peer_ip, local_ip, reset_tuple.remote_port,
		    reset_tuple.local_port, 0x4000, 0x3001,
		    TCP_FLAG_SYN | TCP_FLAG_ACK, bytes, sizeof(bytes), &length));
		assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip,
		    bytes, length, 201, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
		assert(input.state == TCP_STATE_ESTABLISHED);
		assert(build_tcp_bytes(peer_ip, local_ip, reset_tuple.remote_port,
		    reset_tuple.local_port, 0x4001, 0x3001,
		    TCP_FLAG_RST | TCP_FLAG_ACK, bytes, sizeof(bytes), &length));
		assert(tcp_runtime_test_active_input(active.handle, peer_ip, local_ip,
		    bytes, length, 202, &input) == TCP_RUNTIME_TEST_ACTIVE_OK);
		assert(input.state == TCP_STATE_CLOSED);
		assert(tcp_runtime_test_read(active.handle, readback,
		    sizeof(readback), &io) == TCP_RUNTIME_TEST_IO_RESET);
		assert(tcp_runtime_test_write(active.handle, application, 1, 203, &io) ==
		    TCP_RUNTIME_TEST_IO_RESET);
		assert(tcp_runtime_test_release(active.handle));
	}
	puts("TCP Runtime simulated peer: SYN/data ACK/retry/read/FIN/EOF lifecycle PASS");
}

static void
test_runtime_lifecycle_and_capacity(void)
{
	struct tcp_connection_handle handles[TCP_CORE_CONNECTION_LIMIT];
	struct tcp_connection_handle replacement;
	struct tcp_connection_handle failed = {.slot = 0xa5, .generation = 0xdeadbeef};
	struct tcp_core_tuple candidate;
	struct tcp_runtime_test_core_result result;
	struct tcp_runtime_timer_result timers;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_io_result io;
	struct tcp_runtime_test_tx_result tx;
	uint8_t io_byte = 0;
	assert(!tcp_runtime_ready());
	assert(!tcp_runtime_shutdown()); /* shutdown before init */
	assert(tcp_runtime_service_timers(0, TCP_RUNTIME_TIMER_EVENT_LIMIT,
	    &timers) == TCP_RUNTIME_TIMER_DISABLED);
	{
		struct tcp_runtime_test_active_result active;
		candidate = tuple_for(4450);
		assert(tcp_runtime_test_active_open(&candidate, 1, 0, &active) ==
		    TCP_RUNTIME_TEST_ACTIVE_DISABLED);
		assert(!active.has_reference);
		assert(tcp_runtime_test_local_close(
		    (struct tcp_connection_handle){0}, 0, &close_result) ==
		    TCP_RUNTIME_TEST_CLOSE_DISABLED);
		assert(tcp_runtime_test_write((struct tcp_connection_handle){0},
		    NULL, 0, 0, &io) == TCP_RUNTIME_TEST_IO_DISABLED);
		assert(tcp_runtime_test_read((struct tcp_connection_handle){0},
		    &io_byte, 1, &io) == TCP_RUNTIME_TEST_IO_DISABLED);
		assert(tcp_runtime_test_next_segment((struct tcp_connection_handle){0},
		    &io_byte, 1, &tx) == TCP_RUNTIME_TEST_TX_DISABLED);
	}
	assert(tcp_runtime_init());
	assert(tcp_runtime_ready());
	assert(!tcp_runtime_init()); /* duplicate initialization */
	assert(tcp_runtime_service_timers(0, TCP_RUNTIME_TIMER_EVENT_LIMIT,
	    &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 0 && !timers.more_due);
	timers = (struct tcp_runtime_timer_result){
		.status = TCP_RUNTIME_TIMER_OK,
		.count = 1,
		.more_due = true,
		.items[0] = {.actions = UINT32_MAX, .timer_armed = true,
		    .accepted_bytes = SIZE_MAX, .has_child = true}
	};
	assert(tcp_runtime_service_timers(0,
	    TCP_RUNTIME_TIMER_EVENT_LIMIT + 1, &timers) ==
	    TCP_RUNTIME_TIMER_INVALID);
	assert(timers.status == TCP_RUNTIME_TIMER_INVALID && timers.count == 0 &&
	    !timers.more_due && timers.items[0].actions == 0 &&
	    timers.items[0].state == TCP_STATE_CLOSED &&
	    !timers.items[0].timer_armed && timers.items[0].accepted_bytes == 0 &&
	    !timers.items[0].has_child);
	test_runtime_passive_flow();
	test_runtime_passive_fin_lifecycle();
	test_runtime_passive_last_ack_reset_and_reuse();
	test_runtime_routing();
	test_runtime_active_open();
	test_runtime_active_fin_and_time_wait();
	test_runtime_rst_in_fin_wait_2_and_closing();
	test_runtime_mixed_capacity();
	test_runtime_local_close_state_policy();
	test_runtime_stream_transport();
	{
		struct tcp_connection_handle timed[6];
		struct tcp_runtime_test_core_result snapshot;
		/* Six expirations are serviced in batches of two. Each result is scalar
		 * only; the runtime consumes the retained core handles before return. */
		for (size_t i = 0; i < 6; i++) {
			candidate = tuple_for((uint16_t)(9300 + i));
			assert(tcp_runtime_test_reserve(&candidate, &timed[i]));
			assert(tcp_runtime_test_activate(timed[i],
			    (uint32_t)(0x1000 + i), &result));
			assert(result.timer_armed && result.timer_deadline == 100);
			assert(result.receive_window == TCP_CORE_RX_CAPACITY);
		}
		assert(tcp_runtime_service_timers(99, 2, &timers) ==
		    TCP_RUNTIME_TIMER_OK);
		assert(timers.count == 0 && !timers.more_due);
		assert(tcp_runtime_service_timers(100, 0, &timers) ==
		    TCP_RUNTIME_TIMER_OK);
		assert(timers.count == 0 && timers.more_due);
		for (size_t batch = 0; batch < 3; batch++) {
			assert(tcp_runtime_service_timers(100, 2, &timers) ==
			    TCP_RUNTIME_TIMER_OK);
			assert(timers.count == 2);
			assert(timers.more_due == (batch < 2));
			for (size_t i = 0; i < timers.count; i++) {
				assert(timers.items[i].state == TCP_STATE_SYN_SENT);
				assert((timers.items[i].actions &
				    (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN)) ==
				    (TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN));
				assert(timers.items[i].retransmissions == 1);
				assert(timers.items[i].timer_deadline == 300);
				assert(timers.items[i].timer_armed);
				assert(timers.items[i].send_window == 0);
				assert(timers.items[i].receive_window == TCP_CORE_RX_CAPACITY);
				assert(timers.items[i].accepted_bytes == 0 &&
				    !timers.items[i].has_child);
			}
		}
		assert(tcp_runtime_service_timers(100, 2, &timers) ==
		    TCP_RUNTIME_TIMER_OK);
		assert(timers.count == 0 && !timers.more_due);
		/* The single-flight guard rejects nested/busy service and shutdown
		 * without moving the owner to STOPPING. */
		assert(tcp_runtime_test_set_timer_service_active(true));
		assert(tcp_runtime_service_timers(100, 2, &timers) ==
		    TCP_RUNTIME_TIMER_BUSY);
		assert(tcp_runtime_test_local_close(timed[0], 100, &close_result) ==
		    TCP_RUNTIME_TEST_CLOSE_BUSY);
		assert(tcp_runtime_test_snapshot(timed[0], &snapshot) &&
		    snapshot.state == TCP_STATE_SYN_SENT);
		assert(!tcp_runtime_shutdown());
		assert(tcp_runtime_ready());
		assert(tcp_runtime_test_set_timer_service_active(false));
		for (size_t i = 0; i < 6; i++) {
			assert(tcp_runtime_test_close(timed[i], 100, &snapshot));
			assert(snapshot.state == TCP_STATE_CLOSED);
			assert(tcp_runtime_test_release(timed[i]));
		}
	}
	{
		struct tcp_connection_handle handle, reused;
		struct tcp_runtime_test_core_result snapshot;
		/* The runtime consumes its service reference even when retry exhaustion
		 * closes table ownership. A separately held caller reference survives. */
		candidate = tuple_for(9400);
		assert(tcp_runtime_test_reserve(&candidate, &handle));
		assert(tcp_runtime_test_activate(handle, 0xabcdef01, &result));
		const uint64_t deadlines[] = {100, 300, 700, 1500, 2300, 3100};
		for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
			assert(tcp_runtime_service_timers(deadlines[i], 1, &timers) ==
			    TCP_RUNTIME_TIMER_OK);
			assert(timers.count == 1 && !timers.more_due);
		}
		assert(timers.items[0].state == TCP_STATE_CLOSED);
		assert((timers.items[0].actions & TCP_ACTION_ABORT) != 0);
		assert(tcp_runtime_test_snapshot(handle, &snapshot));
		assert(snapshot.state == TCP_STATE_CLOSED);
		assert(tcp_runtime_test_release(handle));
		assert(!tcp_runtime_test_snapshot(handle, &snapshot));
		candidate = tuple_for(9401);
		assert(tcp_runtime_test_reserve(&candidate, &reused));
		assert(reused.slot == handle.slot &&
		    reused.generation != handle.generation);
		assert(!tcp_runtime_test_snapshot(handle, &snapshot));
		assert(tcp_runtime_test_release(reused));
	}
	{
		struct tcp_connection_handle listener, child;
		struct tcp_runtime_test_input_result input;
		struct tcp_runtime_test_core_result snapshot;
		struct tcp_wire_segment syn;
		assert(tcp_runtime_listener_open(local_ip, 8084, &listener) ==
		    TCP_RUNTIME_LISTENER_OK);
		assert(make_tcp_segment(peer_ip, local_ip, 48000, 8084, 77, 0,
		    TCP_FLAG_SYN, NULL, 0, &syn));
		assert(tcp_runtime_test_process_segment(peer_ip, local_ip, &syn,
		    0x76543210, 10, &input) ==
		    TCP_RUNTIME_TEST_INPUT_CHILD_CREATED);
		child = input.handle;
		assert(tcp_runtime_test_retain(child));
		assert(tcp_runtime_handle_release(child));
		assert(!tcp_runtime_listener_close(listener));
		/* Child SYN-ACK retries expire at the exact bounded schedule. The final
		 * close removes the sidecar parent link before its service ref is freed. */
		const uint64_t deadlines[] = {110, 310, 710, 1510, 2310, 3110};
		for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); i++) {
			assert(tcp_runtime_service_timers(deadlines[i], 1, &timers) ==
			    TCP_RUNTIME_TIMER_OK);
			assert(timers.count == 1);
		}
		assert(timers.items[0].state == TCP_STATE_CLOSED);
		assert((timers.items[0].actions & TCP_ACTION_ABORT) != 0);
		assert(tcp_runtime_test_snapshot(child, &snapshot));
		assert(snapshot.state == TCP_STATE_CLOSED);
		assert(tcp_runtime_listener_close(listener));
		assert(tcp_runtime_handle_release(listener));
		assert(tcp_runtime_test_release(child));
		assert(!tcp_runtime_test_snapshot(child, &snapshot));
	}
	puts("TCP runtime timers: bounded batches/more_due, retry exhaustion, retained handles, child-link cleanup and shutdown serialization PASS");

	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++) {
		candidate = tuple_for((uint16_t)(443 + i));
		assert(tcp_runtime_test_reserve(&candidate, &handles[i]));
	}
	candidate = tuple_for(9000);
	assert(!tcp_runtime_test_reserve(&candidate, &failed));
	assert(failed.slot == 0xa5 && failed.generation == 0xdeadbeef);
	assert(tcp_runtime_test_retain(handles[0]));

	/* Releasing and reusing one slot changes its generation; the stale handle
	 * cannot release or retain the replacement entry. */
	assert(tcp_runtime_test_release(handles[3]));
	assert(tcp_runtime_test_reserve(&candidate, &replacement));
	assert(replacement.slot == handles[3].slot);
	assert(replacement.generation != handles[3].generation);
	assert(!tcp_runtime_test_retain(handles[3]));
	assert(!tcp_runtime_test_release(handles[3]));
	assert(tcp_runtime_test_release(replacement));

	/* A live modeled connection keeps table ownership after its caller ref is
	 * released, and stopping the owner never invalidates either kind of ref. */
	assert(tcp_runtime_test_activate(handles[1], 0x12345678, &result));
	assert(result.state == TCP_STATE_SYN_SENT &&
	    (result.actions & TCP_ACTION_SEND_SYN) != 0);
	assert(!tcp_runtime_shutdown());
	assert(tcp_runtime_service_timers(0, TCP_RUNTIME_TIMER_EVENT_LIMIT,
	    &timers) == TCP_RUNTIME_TIMER_STOPPING);
	assert(tcp_runtime_listener_open(local_ip, 9999, &failed) ==
	    TCP_RUNTIME_LISTENER_STOPPING);
	{
		struct tcp_wire_segment segment;
		struct tcp_runtime_route_result route;
		assert(make_segment(peer_ip, local_ip, 40000, 443, NULL, 0,
		    &segment));
		assert_route(TCP_RUNTIME_ROUTE_STOPPING, peer_ip, local_ip, &segment,
		    &route);
	}
	assert(!tcp_runtime_ready());
	assert(!tcp_runtime_init()); /* stopping is terminal */
	candidate = tuple_for(9100);
	assert(!tcp_runtime_test_reserve(&candidate, &failed));
	assert(!tcp_runtime_shutdown()); /* retained handles still exist */

	assert(tcp_runtime_test_local_close(handles[1], 0, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_HANDSHAKE_ABORTED);
	assert(close_result.state == TCP_STATE_CLOSED &&
	    close_result.actions == (TCP_ACTION_SEND_RST | TCP_ACTION_ABORT));
	assert(tcp_runtime_test_local_close(handles[1], 0, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED);
	assert(close_result.state == TCP_STATE_CLOSED && close_result.actions == 0);
	assert(tcp_runtime_test_release(handles[1]));
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++) {
		if (i == 1 || i == 3)
			continue;
		if (i == 0)
			assert(tcp_runtime_test_release(handles[i]));
		assert(tcp_runtime_test_release(handles[i]));
	}
	assert(tcp_runtime_shutdown()); /* empty table after refs are released */
	assert(!tcp_runtime_ready());
	assert(!tcp_runtime_shutdown()); /* repeated shutdown */
	assert(!tcp_runtime_init()); /* reinitialization is unsupported */
	assert(tcp_runtime_service_timers(0, TCP_RUNTIME_TIMER_EVENT_LIMIT,
	    &timers) == TCP_RUNTIME_TIMER_DISABLED);
	assert(tcp_runtime_test_local_close(handles[0], 0, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_DISABLED);
	assert(tcp_runtime_test_write(handles[0], NULL, 0, 0, &io) ==
	    TCP_RUNTIME_TEST_IO_DISABLED);
	assert(tcp_runtime_test_read(handles[0], &io_byte, 1, &io) ==
	    TCP_RUNTIME_TEST_IO_DISABLED);
	assert(tcp_runtime_test_next_segment(handles[0], &io_byte, 1, &tx) ==
	    TCP_RUNTIME_TEST_TX_DISABLED);
	{
		struct tcp_runtime_test_active_result active;
		candidate = tuple_for(4450);
		assert(tcp_runtime_test_active_open(&candidate, 1, 0, &active) ==
		    TCP_RUNTIME_TEST_ACTIVE_DISABLED);
		assert(!active.has_reference);
	}
	puts("TCP runtime lifecycle: one-shot init, capacity 8/full rejection, generation-safe reuse, active/ref-held shutdown refusal, clean shutdown PASS");
}

int
main(void)
{
	test_runtime_lifecycle_and_capacity();
	return 0;
}
