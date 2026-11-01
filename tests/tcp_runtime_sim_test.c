#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "kernel/tcp_runtime.h"

#define SIM_PACKET_LIMIT 8U
#define SIM_STRESS_ITERATIONS 10000U
#define SIM_TIMEOUT_STRESS_PERIOD 127U
#define SIM_ENDPOINT_A 0U
#define SIM_ENDPOINT_B 1U
#define SIM_B_ISN 0x76543210U

static const uint8_t endpoint_ip[2][4] = {
	{10, 15, 3, 1}, {10, 15, 3, 2}
};

enum sim_delivery_policy {
	SIM_DELIVER,
	SIM_DELAY,
	SIM_DROP,
	SIM_DUPLICATE
};

struct sim_packet {
	bool occupied;
	uint8_t from;
	uint8_t to;
	uint64_t due;
	uint64_t serial;
	size_t length;
	uint8_t bytes[TCP_WIRE_SEGMENT_MAX];
};

struct endpoint {
	struct tcp_core_tuple tuple;
	struct tcp_connection_handle connection;
	struct tcp_connection_handle listener;
	struct tcp_connection_handle child;
	bool has_connection;
	bool has_listener;
	bool has_child;
};

struct simulator {
	struct sim_packet packets[SIM_PACKET_LIMIT];
	struct endpoint endpoints[2];
	uint64_t next_serial;
	uint32_t emitted;
	uint32_t delivered;
	uint32_t dropped;
	uint32_t duplicated;
	uint8_t last_dropped[TCP_WIRE_SEGMENT_MAX];
	size_t last_dropped_length;
	uint8_t last_emitted[TCP_WIRE_SEGMENT_MAX];
	size_t last_emitted_length;
};

struct received {
	enum tcp_connection_state state;
	uint32_t actions;
	size_t accepted_bytes;
	enum tcp_runtime_test_active_status active_status;
	enum tcp_runtime_test_input_status passive_status;
};

static void
select_endpoint(uint8_t endpoint)
{
	assert(endpoint < 2);
	assert(tcp_runtime_test_select_instance(endpoint));
}

static bool
parse_packet(const struct sim_packet *packet, struct tcp_wire_segment *segment)
{
	struct tcp_wire_pseudo_header pseudo = {0};
	memcpy(pseudo.source, endpoint_ip[packet->from], 4);
	memcpy(pseudo.destination, endpoint_ip[packet->to], 4);
	pseudo.protocol = 6;
	pseudo.segment_length = (uint16_t)packet->length;
	return tcp_wire_parse(&pseudo, packet->bytes, packet->length, segment) ==
	    TCP_WIRE_OK;
}

static size_t
free_packet_slots(const struct simulator *sim)
{
	size_t free_count = 0;
	for (size_t i = 0; i < SIM_PACKET_LIMIT; i++)
		free_count += !sim->packets[i].occupied;
	return free_count;
}

static bool
try_queue_bytes(struct simulator *sim, uint8_t from, const uint8_t *bytes,
	size_t length, uint64_t now, enum sim_delivery_policy policy,
	uint64_t delay)
{
	struct sim_packet packet = {0};
	size_t copies;
	assert(from < 2 && bytes != NULL && length >= TCP_WIRE_HEADER_MIN &&
	    length <= TCP_WIRE_SEGMENT_MAX);
	if (policy == SIM_DROP) {
		memcpy(sim->last_dropped, bytes, length);
		sim->last_dropped_length = length;
		sim->dropped++;
		return true;
	}
	copies = policy == SIM_DUPLICATE ? 2 : 1;
	if (free_packet_slots(sim) < copies)
		return false;
	packet.occupied = true;
	packet.from = from;
	packet.to = from ^ 1U;
	packet.due = now + (policy == SIM_DELAY ? delay : 0);
	packet.length = length;
	memcpy(packet.bytes, bytes, length);
	memcpy(sim->last_emitted, bytes, length);
	sim->last_emitted_length = length;
	for (size_t copy = 0; copy < copies; copy++) {
		bool inserted = false;
		packet.serial = sim->next_serial++;
		for (size_t i = 0; i < SIM_PACKET_LIMIT; i++) {
			if (!sim->packets[i].occupied) {
				sim->packets[i] = packet;
				inserted = true;
				break;
			}
		}
		assert(inserted);
	}
	if (copies == 2)
		sim->duplicated++;
	return true;
}

static void
queue_bytes(struct simulator *sim, uint8_t from, const uint8_t *bytes,
	size_t length, uint64_t now, enum sim_delivery_policy policy,
	uint64_t delay)
{
	assert(try_queue_bytes(sim, from, bytes, length, now, policy, delay));
}

static void
test_packet_queue_exhaustion(void)
{
	struct simulator sim = {0};
	uint8_t bytes[TCP_WIRE_HEADER_MIN] = {0};
	bytes[0] = 0x10;
	bytes[1] = 0x01;
	bytes[2] = 0x10;
	bytes[3] = 0x02;
	bytes[12] = 0x50;
	for (size_t i = 0; i < SIM_PACKET_LIMIT; i++)
		assert(try_queue_bytes(&sim, SIM_ENDPOINT_A, bytes, sizeof(bytes),
		    100, SIM_DELAY, 5));
	assert(free_packet_slots(&sim) == 0);
	bytes[4] ^= 0xff;
	assert(!try_queue_bytes(&sim, SIM_ENDPOINT_A, bytes, sizeof(bytes),
	    100, SIM_DELIVER, 0));
	assert(free_packet_slots(&sim) == 0);
	for (size_t i = 0; i < SIM_PACKET_LIMIT; i++) {
		assert(sim.packets[i].occupied && sim.packets[i].due == 105);
		assert(sim.packets[i].length == sizeof(bytes));
		assert(sim.packets[i].bytes[4] == 0);
	}
}

static enum tcp_runtime_test_tx_status
runtime_emit(struct simulator *sim, uint8_t from, uint64_t now,
	enum sim_delivery_policy policy, uint64_t delay,
	struct tcp_runtime_test_tx_result *tx)
{
	uint8_t output[TCP_WIRE_SEGMENT_MAX];
	struct tcp_wire_segment parsed;
	struct endpoint *endpoint = &sim->endpoints[from];
	enum tcp_runtime_test_tx_status status;
	assert(endpoint->has_connection || (from == SIM_ENDPOINT_B &&
	    endpoint->has_child));
	select_endpoint(from);
	status = tcp_runtime_test_next_segment(endpoint->has_connection ?
	    endpoint->connection : endpoint->child, output, sizeof(output), tx);
	if (status != TCP_RUNTIME_TEST_TX_OK)
		return status;
	assert(tx->length <= TCP_CORE_MAX_SEGMENT);
	/* Parse the actual bytes with the endpoint pseudo-header and validate the
	 * checksum before the delivery engine takes its owned copy. */
	{
		struct sim_packet view = {.from = from, .to = (uint8_t)(from ^ 1U),
		    .length = tx->length};
		memcpy(view.bytes, output, tx->length);
		assert(parse_packet(&view, &parsed));
	}
	assert(parsed.source_port == endpoint->tuple.local_port);
	assert(parsed.destination_port == endpoint->tuple.remote_port);
	sim->emitted++;
	queue_bytes(sim, from, output, tx->length, now, policy, delay);
	return status;
}

static bool
deliver_packet(struct simulator *sim, size_t index, uint64_t now,
	struct received *received)
{
	struct sim_packet packet;
	struct tcp_wire_segment segment;
	uint8_t to;
	assert(index < SIM_PACKET_LIMIT && received != NULL);
	if (!sim->packets[index].occupied || sim->packets[index].due > now)
		return false;
	packet = sim->packets[index];
	sim->packets[index] = (struct sim_packet){0};
	assert(parse_packet(&packet, &segment));
	to = packet.to;
	select_endpoint(to);
	*received = (struct received){0};
	if (to == SIM_ENDPOINT_A) {
		struct tcp_runtime_test_active_result result;
		assert(sim->endpoints[to].has_connection);
		received->active_status = tcp_runtime_test_active_input(
		    sim->endpoints[to].connection, endpoint_ip[packet.from],
		    endpoint_ip[to], packet.bytes, packet.length, now, &result);
		received->state = result.state;
		received->actions = result.actions;
		received->accepted_bytes = result.accepted_bytes;
		assert(received->active_status == TCP_RUNTIME_TEST_ACTIVE_OK ||
		    received->active_status == TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH);
	} else {
		struct tcp_runtime_test_input_result result;
		received->passive_status = tcp_runtime_test_process_segment(
		    endpoint_ip[packet.from], endpoint_ip[to], &segment, SIM_B_ISN,
		    now, &result);
		received->state = result.outcome.state;
		received->actions = result.outcome.actions;
		received->accepted_bytes = result.outcome.accepted_bytes;
		if (received->passive_status == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED) {
			assert(!sim->endpoints[to].has_child && result.has_reference);
			sim->endpoints[to].child = result.handle;
			sim->endpoints[to].has_child = true;
			memcpy(sim->endpoints[to].tuple.local_address,
			    endpoint_ip[to], 4);
			memcpy(sim->endpoints[to].tuple.remote_address,
			    endpoint_ip[packet.from], 4);
			sim->endpoints[to].tuple.local_port = segment.destination_port;
			sim->endpoints[to].tuple.remote_port = segment.source_port;
		} else if (received->passive_status ==
		    TCP_RUNTIME_TEST_INPUT_DUPLICATE_SYN ||
		    received->passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED) {
			if (received->passive_status ==
			    TCP_RUNTIME_TEST_INPUT_DUPLICATE_SYN)
				assert(result.handle.slot ==
				    sim->endpoints[to].child.slot &&
				    result.handle.generation ==
				    sim->endpoints[to].child.generation);
			assert(result.has_reference);
			assert(tcp_runtime_handle_release(result.handle));
		}
	}
	sim->delivered++;
	return true;
}

static size_t
packet_index(const struct simulator *sim, size_t ordinal)
{
	for (size_t i = 0; i < SIM_PACKET_LIMIT; i++)
		if (sim->packets[i].occupied && ordinal-- == 0)
			return i;
	assert(false && "packet ordinal not found");
	return 0;
}

static void
deliver_only_packet(struct simulator *sim, uint64_t now,
	struct received *received)
{
	size_t index = packet_index(sim, 0);
	if (!deliver_packet(sim, index, now, received)) {
		fprintf(stderr, "packet not due: now=%llu due=%llu serial=%llu\n",
		    (unsigned long long)now,
		    (unsigned long long)sim->packets[index].due,
		    (unsigned long long)sim->packets[index].serial);
		assert(false);
	}
}

static void
build_peer_segment(uint8_t from, uint16_t source_port, uint16_t destination_port,
	uint32_t sequence, uint32_t acknowledgment, uint8_t flags, uint16_t window,
	const uint8_t *payload, size_t payload_length, uint8_t *output,
	size_t capacity, size_t *length)
{
	struct tcp_wire_spec spec = {
		.source_port = source_port,
		.destination_port = destination_port,
		.sequence = sequence,
		.acknowledgment = acknowledgment,
		.flags = flags,
		.window = window,
		.payload = payload,
		.payload_length = payload_length
	};
	assert(tcp_wire_build(endpoint_ip[from], endpoint_ip[from ^ 1U], &spec,
	    output, capacity, length) == TCP_WIRE_OK);
}

static void
simulated_end_to_end(void)
{
	struct simulator sim = {0};
	struct tcp_runtime_test_active_result active;
	struct tcp_runtime_test_close_result close_result;
	struct tcp_runtime_test_io_result io;
	struct tcp_runtime_test_tx_result tx;
	struct tcp_runtime_timer_result timers;
	struct tcp_runtime_test_core_result a_state, b_state;
	struct received received;
	uint8_t client_data[1500], server_data[TCP_CORE_RX_CAPACITY];
	uint8_t readback[TCP_CORE_RX_CAPACITY];
	uint8_t fixture[TCP_WIRE_SEGMENT_MAX];
	uint8_t first_data_packet[TCP_WIRE_SEGMENT_MAX];
	uint8_t second_data_packet[TCP_WIRE_SEGMENT_MAX];
	size_t fixture_length;
	size_t first_data_length = 0;
	size_t second_data_length = 0;
	for (size_t i = 0; i < sizeof(client_data); i++)
		client_data[i] = (uint8_t)(i * 13U + 7U);
	for (size_t i = 0; i < sizeof(server_data); i++)
		server_data[i] = (uint8_t)(i * 17U + 3U);

	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_init());
	sim.endpoints[SIM_ENDPOINT_A].tuple = (struct tcp_core_tuple){
		.local_address = {10, 15, 3, 1}, .remote_address = {10, 15, 3, 2},
		.local_port = 42000, .remote_port = 8088
	};
	sim.endpoints[SIM_ENDPOINT_A].has_connection = true;
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_init());
	assert(tcp_runtime_listener_open(endpoint_ip[SIM_ENDPOINT_B], 8088,
	    &sim.endpoints[SIM_ENDPOINT_B].listener) == TCP_RUNTIME_LISTENER_OK);
	sim.endpoints[SIM_ENDPOINT_B].has_listener = true;
	sim.endpoints[SIM_ENDPOINT_B].tuple = (struct tcp_core_tuple){
		.local_address = {10, 15, 3, 2}, .remote_address = {10, 15, 3, 1},
		.local_port = 8088, .remote_port = 42000
	};
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_active_open(&sim.endpoints[SIM_ENDPOINT_A].tuple,
	    0xfffffff0U, 0, &active) == TCP_RUNTIME_TEST_ACTIVE_OK);
	sim.endpoints[SIM_ENDPOINT_A].connection = active.handle;

	/* Lose SYN, then retransmit a byte-identical SYN twice. The packet queue
	 * owns both copies and delivers them in reverse insertion order. */
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 0, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	assert(sim.dropped == 1 && free_packet_slots(&sim) == SIM_PACKET_LIMIT);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_service_timers(100, 1, &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && (timers.items[0].actions &
	    (TCP_ACTION_SEND_SYN | TCP_ACTION_RETRANSMIT)) ==
	    (TCP_ACTION_SEND_SYN | TCP_ACTION_RETRANSMIT));
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 100, SIM_DUPLICATE, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	assert(sim.packets[packet_index(&sim, 0)].length ==
	    sim.packets[packet_index(&sim, 1)].length);
	assert(memcmp(sim.packets[packet_index(&sim, 0)].bytes,
	    sim.packets[packet_index(&sim, 1)].bytes,
	    sim.packets[packet_index(&sim, 0)].length) == 0);
	deliver_only_packet(&sim, 100, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_CHILD_CREATED &&
	    received.state == TCP_STATE_LISTEN);
	{
		size_t second = packet_index(&sim, 0);
		assert(deliver_packet(&sim, second, 100, &received));
		assert(received.passive_status ==
		    TCP_RUNTIME_TEST_INPUT_DUPLICATE_SYN &&
		    received.state == TCP_STATE_SYN_RECEIVED);
	}
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_B].child,
	    &b_state) && b_state.state == TCP_STATE_SYN_RECEIVED);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 100, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK); /* lose SYN-ACK */
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_service_timers(200, 1, &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && timers.items[0].state == TCP_STATE_SYN_RECEIVED);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 200, SIM_DUPLICATE, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	/* Reorder equal duplicate SYN-ACK copies, proving repeated SYN-ACK does not
	 * create another connection or advance the active sequence state twice. */
	{
		size_t second = packet_index(&sim, 1);
		assert(deliver_packet(&sim, second, 200, &received));
		assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
		    received.state == TCP_STATE_ESTABLISHED);
		deliver_only_packet(&sim, 200, &received);
		assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
		    received.state == TCP_STATE_ESTABLISHED &&
		    received.actions == TCP_ACTION_SEND_ACK);
	}
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 200, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK); /* lose final ACK */
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_service_timers(400, 1, &timers) == TCP_RUNTIME_TIMER_OK);
	assert(timers.count == 1 && (timers.items[0].actions &
	    TCP_ACTION_RETRANSMIT) != 0);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 400, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 400, &received);
	assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
	    received.state == TCP_STATE_ESTABLISHED &&
	    received.actions == TCP_ACTION_SEND_ACK);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 400, SIM_DUPLICATE, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 400, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.state == TCP_STATE_ESTABLISHED);
	deliver_only_packet(&sim, 400, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.state == TCP_STATE_ESTABLISHED);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_A].connection,
	    &a_state) && a_state.state == TCP_STATE_ESTABLISHED &&
	    a_state.snd_nxt == 0xfffffff1U && a_state.rcv_nxt == SIM_B_ISN + 1U);
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_B].child,
	    &b_state) && b_state.state == TCP_STATE_ESTABLISHED &&
	    b_state.snd_nxt == SIM_B_ISN + 1U && b_state.rcv_nxt == 0xfffffff1U);

	/* A's 1500-byte stream crosses MSS. Drop its first segment, then retransmit
	 * identical bytes. Delay the ACK and drop the ACK for the second segment;
	 * duplicate data is acknowledged but delivered only once. */
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_write(sim.endpoints[SIM_ENDPOINT_A].connection,
	    client_data, sizeof(client_data), 401, &io) == TCP_RUNTIME_TEST_IO_OK &&
	    io.bytes == sizeof(client_data));
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 401, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	assert(tx.length == TCP_WIRE_HEADER_MIN + TCP_CORE_LOCAL_MSS &&
	    (tx.actions & TCP_ACTION_SEND_DATA) != 0);
	{
		uint8_t original[TCP_WIRE_SEGMENT_MAX];
		size_t original_length = sim.last_dropped_length;
		memcpy(original, sim.last_dropped, original_length);
		select_endpoint(SIM_ENDPOINT_A);
		assert(tcp_runtime_service_timers(501, 1, &timers) ==
		    TCP_RUNTIME_TIMER_OK && timers.count == 1);
		assert(runtime_emit(&sim, SIM_ENDPOINT_A, 501, SIM_DELIVER, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		assert(tx.length == original_length &&
		    memcmp(sim.last_emitted, original, original_length) == 0);
		memcpy(first_data_packet, sim.last_emitted, tx.length);
		first_data_length = tx.length;
	}
	deliver_only_packet(&sim, 501, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.accepted_bytes == TCP_CORE_LOCAL_MSS &&
	    received.state == TCP_STATE_ESTABLISHED);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 501, SIM_DELAY, 2, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	assert(!deliver_packet(&sim, packet_index(&sim, 0), 502, &received));
	assert(deliver_packet(&sim, packet_index(&sim, 0), 503, &received));
	assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
	    (received.actions & TCP_ACTION_SEND_DATA) != 0);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 503, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	{
		uint8_t original[TCP_WIRE_SEGMENT_MAX];
		size_t original_length = sim.last_dropped_length;
		memcpy(original, sim.last_dropped, original_length);
		select_endpoint(SIM_ENDPOINT_A);
		assert(tcp_runtime_service_timers(603, 1, &timers) ==
		    TCP_RUNTIME_TIMER_OK && timers.count == 1);
		assert(runtime_emit(&sim, SIM_ENDPOINT_A, 603, SIM_DELIVER, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		assert(tx.length == original_length &&
		    memcmp(sim.last_emitted, original, original_length) == 0);
		memcpy(second_data_packet, sim.last_emitted, tx.length);
		second_data_length = tx.length;
	}
	deliver_only_packet(&sim, 603, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.accepted_bytes == sizeof(client_data) - TCP_CORE_LOCAL_MSS);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 603, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK); /* lose ACK after data reached B */
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_service_timers(803, 1, &timers) == TCP_RUNTIME_TIMER_OK &&
	    timers.count == 1 && (timers.items[0].actions & TCP_ACTION_RETRANSMIT));
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 803, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	assert(tx.length == second_data_length &&
	    memcmp(sim.last_emitted, second_data_packet, second_data_length) == 0);
	deliver_only_packet(&sim, 803, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.accepted_bytes == 0 &&
	    (received.actions & TCP_ACTION_SEND_ACK) != 0);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 803, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 803, &received);
	assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
	    received.accepted_bytes == 0);
	assert(first_data_length == TCP_WIRE_HEADER_MIN + TCP_CORE_LOCAL_MSS);
	queue_bytes(&sim, SIM_ENDPOINT_A, first_data_packet, first_data_length, 803,
	    SIM_DUPLICATE, 0);
	deliver_only_packet(&sim, 803, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.accepted_bytes == 0 &&
	    (received.actions & TCP_ACTION_SEND_ACK) != 0);
	deliver_only_packet(&sim, 803, &received);
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED &&
	    received.accepted_bytes == 0);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 803, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 803, &received);
	assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
	    received.accepted_bytes == 0 && received.actions == 0);
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_read(sim.endpoints[SIM_ENDPOINT_B].child,
	    readback, 777, &io) == TCP_RUNTIME_TEST_IO_OK && io.bytes == 777);
	assert(memcmp(readback, client_data, 777) == 0);
	assert(tcp_runtime_test_read(sim.endpoints[SIM_ENDPOINT_B].child,
	    readback, sizeof(readback), &io) == TCP_RUNTIME_TEST_IO_OK &&
	    io.bytes == sizeof(client_data) - 777);
	assert(memcmp(readback, client_data + 777, io.bytes) == 0);

	/* Reorder B's two MSS-bounded data segments: inject the valid tail first,
	 * verify A holds RCV.NXT, then deliver Core's first segment and its real
	 * next segment after ACK. The tail fixture uses the identical wire fields. */
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_write(sim.endpoints[SIM_ENDPOINT_B].child,
	    server_data, sizeof(server_data), 804, &io) == TCP_RUNTIME_TEST_IO_OK);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 804, SIM_DELAY, 5, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	{
		struct tcp_core_tuple tuple = sim.endpoints[SIM_ENDPOINT_B].tuple;
		struct tcp_wire_segment expected;
		uint32_t sequence;
		uint32_t acknowledgment;
		select_endpoint(SIM_ENDPOINT_B);
		assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_B].child,
		    &b_state));
		sequence = b_state.snd_nxt;
		acknowledgment = b_state.rcv_nxt;
		build_peer_segment(SIM_ENDPOINT_B, tuple.local_port,
		    tuple.remote_port, sequence, acknowledgment, TCP_FLAG_ACK,
		    TCP_CORE_RX_CAPACITY, server_data + TCP_CORE_LOCAL_MSS,
		    sizeof(server_data) - TCP_CORE_LOCAL_MSS, fixture, sizeof(fixture),
		    &fixture_length);
		queue_bytes(&sim, SIM_ENDPOINT_B, fixture, fixture_length, 804,
		    SIM_DELIVER, 0);
		{
			size_t future = packet_index(&sim, 1);
			assert(deliver_packet(&sim, future, 804, &received));
			assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
			    received.accepted_bytes == 0 &&
			    received.state == TCP_STATE_ESTABLISHED);
		}
		select_endpoint(SIM_ENDPOINT_A);
		assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_A].connection,
		    &a_state) && a_state.rcv_nxt == SIM_B_ISN + 1U);
		assert(runtime_emit(&sim, SIM_ENDPOINT_A, 804, SIM_DELIVER, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		assert(deliver_packet(&sim, packet_index(&sim, 1), 804, &received));
		assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
		assert(deliver_packet(&sim, packet_index(&sim, 0), 809, &received));
		assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
		    received.accepted_bytes == TCP_CORE_LOCAL_MSS);
		assert(runtime_emit(&sim, SIM_ENDPOINT_A, 809, SIM_DELIVER, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		deliver_only_packet(&sim, 809, &received);
		assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
		assert(runtime_emit(&sim, SIM_ENDPOINT_B, 809, SIM_DELIVER, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		{
			struct sim_packet actual = {.from = SIM_ENDPOINT_B,
			    .to = SIM_ENDPOINT_A, .length = tx.length};
			memcpy(actual.bytes, sim.last_emitted, tx.length);
			assert(parse_packet(&actual, &expected));
			assert(expected.sequence == sequence &&
			    expected.acknowledgment == acknowledgment &&
			    expected.payload_length == sizeof(server_data) -
			    TCP_CORE_LOCAL_MSS &&
			    memcmp(expected.payload, server_data + TCP_CORE_LOCAL_MSS,
			    expected.payload_length) == 0);
		}
		deliver_only_packet(&sim, 809, &received);
		assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
		    received.accepted_bytes == sizeof(server_data) -
		    TCP_CORE_LOCAL_MSS);
		assert(runtime_emit(&sim, SIM_ENDPOINT_A, 809, SIM_DELIVER, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		deliver_only_packet(&sim, 809, &received);
		assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
	}
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_read(sim.endpoints[SIM_ENDPOINT_A].connection,
	    readback, sizeof(readback), &io) == TCP_RUNTIME_TEST_IO_OK &&
	    io.bytes == sizeof(server_data));
	assert(memcmp(readback, server_data, sizeof(server_data)) == 0);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 809, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 809, &received); /* advertise reopened window */
	assert(received.passive_status == TCP_RUNTIME_TEST_INPUT_ESTABLISHED);

	/* Fill A's receive ring via B. B's subsequent write remains queued at a
	 * zero advertised window; A's partial read emits a window update and resumes
	 * B's queued data. */
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_write(sim.endpoints[SIM_ENDPOINT_B].child,
	    server_data, TCP_CORE_RX_CAPACITY, 810, &io) == TCP_RUNTIME_TEST_IO_OK);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 810, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 810, &received);
	assert(received.accepted_bytes == TCP_CORE_LOCAL_MSS);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 810, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 810, &received);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 810, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 810, &received);
	assert(received.accepted_bytes == TCP_CORE_RX_CAPACITY -
	    TCP_CORE_LOCAL_MSS && received.state == TCP_STATE_ESTABLISHED);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 810, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 810, &received);
	assert(received.state == TCP_STATE_ESTABLISHED);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_A].connection,
	    &a_state) && a_state.receive_window == 0);
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_write(sim.endpoints[SIM_ENDPOINT_B].child,
	    (const uint8_t *)"window", 6, 811, &io) == TCP_RUNTIME_TEST_IO_OK &&
	    io.bytes == 6);
	assert(tcp_runtime_test_next_segment(sim.endpoints[SIM_ENDPOINT_B].child,
	    fixture, sizeof(fixture), &tx) == TCP_RUNTIME_TEST_TX_EMPTY);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_read(sim.endpoints[SIM_ENDPOINT_A].connection,
	    readback, 512, &io) == TCP_RUNTIME_TEST_IO_OK && io.bytes == 512);
	assert(tcp_runtime_test_next_segment(sim.endpoints[SIM_ENDPOINT_A].connection,
	    fixture, sizeof(fixture), &tx) == TCP_RUNTIME_TEST_TX_OK);
	queue_bytes(&sim, SIM_ENDPOINT_A, fixture, tx.length, 811, SIM_DELIVER, 0);
	deliver_only_packet(&sim, 811, &received);
	assert(received.actions & TCP_ACTION_SEND_DATA);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 811, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 811, &received);
	assert(received.accepted_bytes == 6);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 811, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 811, &received);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_read(sim.endpoints[SIM_ENDPOINT_A].connection,
	    readback, sizeof(readback), &io) == TCP_RUNTIME_TEST_IO_OK &&
	    io.bytes == TCP_CORE_RX_CAPACITY - 512 + 6);
	assert(memcmp(readback, server_data + 512,
	    TCP_CORE_RX_CAPACITY - 512) == 0 &&
	    memcmp(readback + TCP_CORE_RX_CAPACITY - 512, "window", 6) == 0);

	/* Lose the first FIN. The retry is duplicated; the peer consumes sequence
	 * space only once, sends one cumulative ACK, and reaches TIME-WAIT only on
	 * the peer FIN. */
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_local_close(sim.endpoints[SIM_ENDPOINT_A].connection,
	    900, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 900, SIM_DROP, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_service_timers(1000, 1, &timers) == TCP_RUNTIME_TIMER_OK &&
	    timers.count == 1 &&
	    (timers.items[0].actions & TCP_ACTION_RETRANSMIT));
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 1000, SIM_DUPLICATE, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 1000, &received);
	assert(received.state == TCP_STATE_CLOSE_WAIT);
	deliver_only_packet(&sim, 1000, &received);
	assert(received.state == TCP_STATE_CLOSE_WAIT);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 1000, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 1000, &received);
	assert(received.state == TCP_STATE_FIN_WAIT_2);
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_local_close(sim.endpoints[SIM_ENDPOINT_B].child,
	    1001, &close_result) ==
	    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
	assert(runtime_emit(&sim, SIM_ENDPOINT_B, 1001, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 1001, &received);
	assert(received.state == TCP_STATE_TIME_WAIT &&
	    (received.actions & TCP_ACTION_SEND_ACK));
	assert(runtime_emit(&sim, SIM_ENDPOINT_A, 1001, SIM_DELIVER, 0, &tx) ==
	    TCP_RUNTIME_TEST_TX_OK);
	deliver_only_packet(&sim, 1001, &received);
	assert(received.state == TCP_STATE_CLOSED);
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_service_timers(13001, 1, &timers) == TCP_RUNTIME_TIMER_OK &&
	    timers.count == 1 && timers.items[0].state == TCP_STATE_CLOSED);
	assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_A].connection,
	    &a_state) && a_state.state == TCP_STATE_CLOSED);
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_B].child,
	    &b_state) && b_state.state == TCP_STATE_CLOSED);
	assert(tcp_runtime_handle_release(sim.endpoints[SIM_ENDPOINT_B].child));
	sim.endpoints[SIM_ENDPOINT_B].has_child = false;
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_test_release(sim.endpoints[SIM_ENDPOINT_A].connection));
	sim.endpoints[SIM_ENDPOINT_A].has_connection = false;
	{
		struct tcp_connection_handle stale_a = sim.endpoints[SIM_ENDPOINT_A].connection;
		struct tcp_connection_handle stale_b = sim.endpoints[SIM_ENDPOINT_B].child;
		uint8_t previous_generation_packet[TCP_WIRE_SEGMENT_MAX];
		size_t previous_generation_length = 0;
		uint64_t now = 13000;
		for (size_t iteration = 0; iteration < SIM_STRESS_ITERATIONS;
		    iteration++) {
			struct tcp_core_tuple *tuple = &sim.endpoints[SIM_ENDPOINT_A].tuple;
			struct tcp_runtime_test_close_result closed;
			uint32_t local_isn = 0x10000U + (uint32_t)iteration * 100U;
			tuple->local_port = (uint16_t)(42001U + iteration);
			if (previous_generation_length != 0) {
				assert(try_queue_bytes(&sim, SIM_ENDPOINT_A,
				    previous_generation_packet,
				    previous_generation_length, now, SIM_DELIVER, 0));
				deliver_only_packet(&sim, now, &received);
				assert(received.passive_status ==
				    TCP_RUNTIME_TEST_INPUT_NO_MATCH);
				assert(!sim.endpoints[SIM_ENDPOINT_B].has_child);
			}
			/* Periodically include a failed active attempt in the stress
			 * sequence. All SYNs are explicitly dropped, and Core's bounded
			 * timeout path must reclaim the slot before the next generation. */
			if (iteration != 0 &&
			    iteration % SIM_TIMEOUT_STRESS_PERIOD == 0) {
				static const uint16_t offsets[] = {
					100, 300, 700, 1500, 2300, 3100
				};
				select_endpoint(SIM_ENDPOINT_A);
				assert(tcp_runtime_test_active_open(tuple,
				    0x90000000U + (uint32_t)iteration, now,
				    &active) == TCP_RUNTIME_TEST_ACTIVE_OK);
				sim.endpoints[SIM_ENDPOINT_A].connection = active.handle;
				sim.endpoints[SIM_ENDPOINT_A].has_connection = true;
				assert(runtime_emit(&sim, SIM_ENDPOINT_A, now, SIM_DROP, 0,
				    &tx) == TCP_RUNTIME_TEST_TX_OK);
				for (size_t retry = 0; retry <
				    sizeof(offsets) / sizeof(offsets[0]); retry++) {
					select_endpoint(SIM_ENDPOINT_A);
					assert(tcp_runtime_service_timers(now + offsets[retry],
					    1, &timers) == TCP_RUNTIME_TIMER_OK &&
					    timers.count == 1);
				}
				assert(timers.items[0].state == TCP_STATE_CLOSED &&
				    (timers.items[0].actions & TCP_ACTION_ABORT));
				assert(tcp_runtime_test_release(active.handle));
				sim.endpoints[SIM_ENDPOINT_A].has_connection = false;
				stale_a = active.handle;
				now += 4000;
				continue;
			}
			select_endpoint(SIM_ENDPOINT_A);
			assert(tcp_runtime_test_active_open(tuple, local_isn, now,
			    &active) == TCP_RUNTIME_TEST_ACTIVE_OK);
			sim.endpoints[SIM_ENDPOINT_A].connection = active.handle;
			sim.endpoints[SIM_ENDPOINT_A].has_connection = true;
			assert(!tcp_runtime_test_snapshot(stale_a, &a_state));
			assert(tcp_runtime_service_timers(now - 1, 1, &timers) ==
			    TCP_RUNTIME_TIMER_OK && timers.count == 0);
			assert(runtime_emit(&sim, SIM_ENDPOINT_A, now, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			deliver_only_packet(&sim, now, &received);
			assert(received.passive_status ==
			    TCP_RUNTIME_TEST_INPUT_CHILD_CREATED &&
			    received.state == TCP_STATE_LISTEN);
			assert(!tcp_runtime_test_snapshot(stale_b, &b_state));
			if (iteration == 0) {
				queue_bytes(&sim, SIM_ENDPOINT_A, first_data_packet,
				    first_data_length, now, SIM_DELIVER, 0);
				deliver_only_packet(&sim, now, &received);
				assert(received.passive_status ==
				    TCP_RUNTIME_TEST_INPUT_NO_MATCH);
				select_endpoint(SIM_ENDPOINT_B);
				assert(tcp_runtime_test_snapshot(
				    sim.endpoints[SIM_ENDPOINT_B].child, &b_state) &&
				    b_state.state == TCP_STATE_SYN_RECEIVED);
			}
			select_endpoint(SIM_ENDPOINT_B);
			assert(runtime_emit(&sim, SIM_ENDPOINT_B, now, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			deliver_only_packet(&sim, now, &received);
			assert(received.active_status == TCP_RUNTIME_TEST_ACTIVE_OK &&
			    received.state == TCP_STATE_ESTABLISHED);
			assert(runtime_emit(&sim, SIM_ENDPOINT_A, now, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			deliver_only_packet(&sim, now, &received);
			assert(received.passive_status ==
			    TCP_RUNTIME_TEST_INPUT_ESTABLISHED);
			select_endpoint(SIM_ENDPOINT_A);
			assert(tcp_runtime_test_local_close(
			    sim.endpoints[SIM_ENDPOINT_A].connection, now + 1,
			    &closed) == TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
			assert(runtime_emit(&sim, SIM_ENDPOINT_A, now + 1, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			deliver_only_packet(&sim, now + 1, &received);
			assert(received.state == TCP_STATE_CLOSE_WAIT);
			assert(runtime_emit(&sim, SIM_ENDPOINT_B, now + 1, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			deliver_only_packet(&sim, now + 1, &received);
			assert(received.state == TCP_STATE_FIN_WAIT_2);
			select_endpoint(SIM_ENDPOINT_B);
			assert(tcp_runtime_test_local_close(
			    sim.endpoints[SIM_ENDPOINT_B].child, now + 2,
			    &closed) == TCP_RUNTIME_TEST_CLOSE_FIN_STARTED);
			assert(runtime_emit(&sim, SIM_ENDPOINT_B, now + 2, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			deliver_only_packet(&sim, now + 2, &received);
			assert(received.state == TCP_STATE_TIME_WAIT);
			assert(runtime_emit(&sim, SIM_ENDPOINT_A, now + 2, SIM_DELIVER, 0,
			    &tx) == TCP_RUNTIME_TEST_TX_OK);
			previous_generation_length = tx.length;
			memcpy(previous_generation_packet, sim.last_emitted, tx.length);
			deliver_only_packet(&sim, now + 2, &received);
			assert(received.state == TCP_STATE_CLOSED);
			select_endpoint(SIM_ENDPOINT_A);
			assert(tcp_runtime_service_timers(now + 2 +
			    TCP_CORE_TIME_WAIT_TICKS, 1, &timers) ==
			    TCP_RUNTIME_TIMER_OK && timers.count == 1 &&
			    timers.items[0].state == TCP_STATE_CLOSED);
			assert(tcp_runtime_test_release(
			    sim.endpoints[SIM_ENDPOINT_A].connection));
			select_endpoint(SIM_ENDPOINT_B);
			assert(tcp_runtime_test_release(sim.endpoints[SIM_ENDPOINT_B].child));
			sim.endpoints[SIM_ENDPOINT_A].has_connection = false;
			sim.endpoints[SIM_ENDPOINT_B].has_child = false;
			stale_a = sim.endpoints[SIM_ENDPOINT_A].connection;
			stale_b = sim.endpoints[SIM_ENDPOINT_B].child;
			now += TCP_CORE_TIME_WAIT_TICKS + 100;
		}
		/* Bounded SYN retry exhaustion is the failure counterpart to the
		 * successful lossy handshake above. No dropped packet is auto-delivered. */
		select_endpoint(SIM_ENDPOINT_A);
		sim.endpoints[SIM_ENDPOINT_A].tuple.local_port = 42100;
		assert(tcp_runtime_test_active_open(
		    &sim.endpoints[SIM_ENDPOINT_A].tuple, 0xb000, now,
		    &active) == TCP_RUNTIME_TEST_ACTIVE_OK);
		sim.endpoints[SIM_ENDPOINT_A].connection = active.handle;
		sim.endpoints[SIM_ENDPOINT_A].has_connection = true;
		assert(runtime_emit(&sim, SIM_ENDPOINT_A, now, SIM_DROP, 0, &tx) ==
		    TCP_RUNTIME_TEST_TX_OK);
		{
			uint8_t original[TCP_WIRE_SEGMENT_MAX];
			size_t original_length = sim.last_dropped_length;
			static const uint16_t offsets[] = {100, 300, 700, 1500, 2300, 3100};
			memcpy(original, sim.last_dropped, original_length);
			for (size_t retry = 0; retry <
			    sizeof(offsets) / sizeof(offsets[0]); retry++) {
				select_endpoint(SIM_ENDPOINT_A);
				assert(tcp_runtime_service_timers(now + offsets[retry], 1,
				    &timers) == TCP_RUNTIME_TIMER_OK && timers.count == 1);
				if (retry + 1 == sizeof(offsets) / sizeof(offsets[0])) {
					assert(timers.items[0].state == TCP_STATE_CLOSED &&
					    (timers.items[0].actions & TCP_ACTION_ABORT));
				} else {
					assert(timers.items[0].state == TCP_STATE_SYN_SENT &&
					    (timers.items[0].actions & TCP_ACTION_RETRANSMIT));
					assert(runtime_emit(&sim, SIM_ENDPOINT_A,
					    now + offsets[retry], SIM_DROP, 0, &tx) ==
					    TCP_RUNTIME_TEST_TX_OK);
					assert(tx.length == original_length &&
					    memcmp(sim.last_dropped, original, original_length) == 0);
				}
				assert(tcp_runtime_service_timers(now + offsets[retry], 1,
				    &timers) == TCP_RUNTIME_TIMER_OK && timers.count == 0 &&
				    !timers.more_due);
			}
		}
		assert(tcp_runtime_test_snapshot(sim.endpoints[SIM_ENDPOINT_A].connection,
		    &a_state) && a_state.state == TCP_STATE_CLOSED);
		assert(tcp_runtime_test_release(sim.endpoints[SIM_ENDPOINT_A].connection));
		sim.endpoints[SIM_ENDPOINT_A].has_connection = false;
	}
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_listener_close(sim.endpoints[SIM_ENDPOINT_B].listener));
	assert(tcp_runtime_handle_release(sim.endpoints[SIM_ENDPOINT_B].listener));
	select_endpoint(SIM_ENDPOINT_B);
	assert(tcp_runtime_shutdown());
	select_endpoint(SIM_ENDPOINT_A);
	assert(tcp_runtime_shutdown());
	assert(sim.dropped >= 5 && sim.duplicated >= 4 && sim.delivered > 20);
	assert(free_packet_slots(&sim) == SIM_PACKET_LIMIT);
	printf("TCP Runtime two-endpoint simulation: emitted=%u delivered=%u dropped=%u duplicated=%u, handshake/data/ACK/FIN recovery, retry exhaustion, reordering/duplicates, zero-window reopen and %u mixed cleanup/reuse iterations (%u injected SYN timeouts) PASS\n",
	    sim.emitted, sim.delivered, sim.dropped, sim.duplicated,
	    SIM_STRESS_ITERATIONS,
	    (SIM_STRESS_ITERATIONS - 1U) / SIM_TIMEOUT_STRESS_PERIOD);
}

int
main(void)
{
	test_packet_queue_exhaustion();
	simulated_end_to_end();
	return 0;
}
