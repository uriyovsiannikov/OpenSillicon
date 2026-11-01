#include "kernel/tcp_core.h"
#include "kernel/tcp_wire.h"

#define TCP_SUPPORTED_FLAGS (TCP_FLAG_FIN | TCP_FLAG_SYN | TCP_FLAG_RST | \
	TCP_FLAG_PSH | TCP_FLAG_ACK)
#define TCP_CONTROL_MASK (TCP_FLAG_SYN | TCP_FLAG_FIN)

static bool
tcp_segment_validate_and_parse(const uint8_t source[4],
	const uint8_t destination[4], const uint8_t *segment, size_t length,
	struct tcp_segment_view *view)
{
	struct tcp_wire_pseudo_header pseudo = {0};
	struct tcp_wire_segment parsed;
	if (source == NULL || destination == NULL || segment == NULL ||
	    length > UINT16_MAX || length > TCP_CORE_MAX_SEGMENT)
		return false;
	for (size_t i = 0; i < 4; i++) {
		pseudo.source[i] = source[i];
		pseudo.destination[i] = destination[i];
	}
	pseudo.protocol = 6;
	pseudo.segment_length = (uint16_t)length;
	if (tcp_wire_parse(&pseudo, segment, length, &parsed) != TCP_WIRE_OK ||
	    (parsed.flags & (TCP_FLAG_URG | TCP_FLAG_ECE | TCP_FLAG_CWR)) != 0)
		return false;
	if (view != NULL) {
		*view = (struct tcp_segment_view){
			.source_port = parsed.source_port,
			.destination_port = parsed.destination_port,
			.sequence = parsed.sequence,
			.acknowledgment = parsed.acknowledgment,
			.flags = parsed.flags,
			.window = parsed.window,
			.mss_present = parsed.parsed_options.mss_present,
			.mss = parsed.parsed_options.mss,
			.payload = parsed.payload,
			.payload_length = parsed.payload_length
		};
	}
	return true;
}

bool
tcp_segment_parse_ipv4(const uint8_t source[4], const uint8_t destination[4],
	const uint8_t *segment, size_t length, struct tcp_segment_view *view)
{
	return view != NULL && tcp_segment_validate_and_parse(source, destination,
	    segment, length, view);
}

bool
tcp_segment_validate_ipv4(const uint8_t source[4],
	const uint8_t destination[4], const uint8_t *segment, size_t length)
{
	return tcp_segment_validate_and_parse(source, destination, segment,
	    length, NULL);
}

bool
tcp_segment_build_ipv4(const uint8_t source[4], const uint8_t destination[4],
	const struct tcp_segment_spec *spec, uint8_t *segment, size_t capacity,
	size_t *length)
{
	uint8_t options[4];
	struct tcp_wire_spec wire_spec;
	if (source == NULL || destination == NULL || spec == NULL ||
	    segment == NULL || length == NULL || spec->source_port == 0 ||
	    spec->destination_port == 0 ||
	    (spec->flags & (TCP_FLAG_URG | TCP_FLAG_ECE | TCP_FLAG_CWR)) != 0 ||
	    (spec->payload_length != 0 && spec->payload == NULL) ||
	    spec->payload_length > TCP_CORE_MAX_SEGMENT - TCP_WIRE_HEADER_MIN ||
	    (spec->include_mss && ((spec->flags & TCP_FLAG_SYN) == 0 ||
	    spec->mss == 0)))
		return false;
	if (spec->include_mss) {
		options[0] = 2;
		options[1] = 4;
		options[2] = (uint8_t)(spec->mss >> 8);
		options[3] = (uint8_t)spec->mss;
	}
	wire_spec = (struct tcp_wire_spec){
		.source_port = spec->source_port,
		.destination_port = spec->destination_port,
		.sequence = spec->sequence,
		.acknowledgment = spec->acknowledgment,
		.flags = spec->flags,
		.window = spec->window,
		.options = spec->include_mss ? options : NULL,
		.options_length = spec->include_mss ? sizeof(options) : 0,
		.payload = spec->payload,
		.payload_length = spec->payload_length
	};
	return tcp_wire_build(source, destination, &wire_spec, segment, capacity,
	    length) == TCP_WIRE_OK;
}

static struct tcp_core_connection *
connection_lookup(struct tcp_connection_table *table,
	struct tcp_connection_handle handle)
{
	struct tcp_core_connection *connection;
	if (table == NULL || handle.slot >= TCP_CORE_CONNECTION_LIMIT ||
	    handle.generation == 0)
		return NULL;
	connection = &table->slots[handle.slot];
	return connection->allocated && connection->generation == handle.generation ?
	    connection : NULL;
}

static const struct tcp_core_connection *
connection_lookup_const(const struct tcp_connection_table *table,
	struct tcp_connection_handle handle)
{
	const struct tcp_core_connection *connection;
	if (table == NULL || handle.slot >= TCP_CORE_CONNECTION_LIMIT ||
	    handle.generation == 0)
		return NULL;
	connection = &table->slots[handle.slot];
	return connection->allocated && connection->generation == handle.generation ?
	    connection : NULL;
}

static void
connection_collect(struct tcp_core_connection *connection)
{
	uint32_t generation = connection->generation;
	if (connection->allocated && !connection->table_owned &&
	    connection->external_references == 0 &&
	    connection->state == TCP_STATE_CLOSED) {
		*connection = (struct tcp_core_connection){.generation = generation};
	}
}

static void
table_reference_drop(struct tcp_core_connection *connection)
{
	connection->table_owned = false;
	connection_collect(connection);
}

static bool
address_equal(const uint8_t a[4], const uint8_t b[4])
{
	for (size_t i = 0; i < 4; i++)
		if (a[i] != b[i])
			return false;
	return true;
}

static bool
address_is_zero(const uint8_t address[4])
{
	static const uint8_t zero[4] = {0};
	return address_equal(address, zero);
}

static bool
tuple_is_listener(const struct tcp_core_tuple *tuple)
{
	return tuple->remote_port == 0;
}

static bool
tuple_valid(const struct tcp_core_tuple *tuple)
{
	return tuple != NULL && tuple->local_port != 0 &&
	    (tuple->remote_port != 0 || address_is_zero(tuple->remote_address));
}

static bool
tuple_exact_equal(const struct tcp_core_tuple *a,
	const struct tcp_core_tuple *b)
{
	return a->local_port == b->local_port &&
	    a->remote_port == b->remote_port &&
	    address_equal(a->local_address, b->local_address) &&
	    address_equal(a->remote_address, b->remote_address);
}

static bool
tuple_conflicts(const struct tcp_core_tuple *a,
	const struct tcp_core_tuple *b)
{
	if (tuple_is_listener(a) && tuple_is_listener(b)) {
		return a->local_port == b->local_port &&
		    (address_is_zero(a->local_address) ||
		    address_is_zero(b->local_address) ||
		    address_equal(a->local_address, b->local_address));
	}
	if (tuple_is_listener(a) != tuple_is_listener(b))
		return false;
	return tuple_exact_equal(a, b);
}

void
tcp_core_table_init(struct tcp_connection_table *table)
{
	if (table != NULL)
		*table = (struct tcp_connection_table){.next_generation = 1};
}

static bool
connection_allocate(struct tcp_connection_table *table,
	const struct tcp_core_tuple *tuple, struct tcp_connection_handle *handle,
	struct tcp_core_connection **allocated)
{
	if (table == NULL || !tuple_valid(tuple) || handle == NULL)
		return false;
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++)
		if (table->slots[i].allocated &&
		    tuple_conflicts(&table->slots[i].tuple, tuple))
			return false;
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++) {
		struct tcp_core_connection *connection = &table->slots[i];
		uint32_t generation;
		if (connection->allocated)
			continue;
		generation = table->next_generation++;
		if (generation == 0) {
			generation = table->next_generation++;
			if (generation == 0)
				generation = 1;
		}
		if (table->next_generation == 0)
			table->next_generation = 1;
		*connection = (struct tcp_core_connection){
			.allocated = true,
			.external_references = 1,
			.generation = generation,
			.state = TCP_STATE_CLOSED,
			.tuple = *tuple,
			.peer_mss = TCP_CORE_DEFAULT_MSS
		};
		*handle = (struct tcp_connection_handle){
			.slot = (uint8_t)i,
			.generation = generation
		};
		if (allocated != NULL)
			*allocated = connection;
		return true;
	}
	return false;
}

bool
tcp_core_connection_create(struct tcp_connection_table *table,
	const struct tcp_core_tuple *tuple, struct tcp_connection_handle *handle)
{
	return connection_allocate(table, tuple, handle, NULL);
}

enum tcp_core_lookup_status
tcp_core_connection_lookup(struct tcp_connection_table *table,
	const struct tcp_core_tuple *tuple, struct tcp_connection_handle *handle)
{
	struct tcp_core_connection *match = NULL;
	struct tcp_core_connection *listener_match = NULL;
	size_t exact_matches = 0;
	size_t listener_matches = 0;
	if (table == NULL || !tuple_valid(tuple) || tuple_is_listener(tuple) ||
	    handle == NULL)
		return TCP_CORE_LOOKUP_INVALID;
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++) {
		struct tcp_core_connection *connection = &table->slots[i];
		if (!connection->allocated || connection->state == TCP_STATE_CLOSED)
			continue;
		if (tuple_is_listener(&connection->tuple)) {
			if (connection->state != TCP_STATE_LISTEN ||
			    connection->tuple.local_port != tuple->local_port ||
			    (!address_is_zero(connection->tuple.local_address) &&
			    !address_equal(connection->tuple.local_address,
			    tuple->local_address)))
				continue;
			listener_match = connection;
			listener_matches++;
			continue;
		}
		if (tuple_exact_equal(&connection->tuple, tuple)) {
			match = connection;
			exact_matches++;
		}
	}
	if (exact_matches > 1 || (exact_matches == 0 && listener_matches > 1))
		return TCP_CORE_LOOKUP_AMBIGUOUS;
	if (exact_matches == 0 && listener_matches == 1)
		match = listener_match;
	else if (exact_matches == 0)
		return TCP_CORE_LOOKUP_NOT_FOUND;
	if (match->external_references == UINT16_MAX)
		return TCP_CORE_LOOKUP_REFERENCE_LIMIT;
	match->external_references++;
	*handle = (struct tcp_connection_handle){
		.slot = (uint8_t)(match - table->slots),
		.generation = match->generation
	};
	return TCP_CORE_LOOKUP_FOUND;
}

bool
tcp_core_connection_retain(struct tcp_connection_table *table,
	struct tcp_connection_handle handle)
{
	struct tcp_core_connection *connection = connection_lookup(table, handle);
	if (connection == NULL || connection->external_references == UINT16_MAX)
		return false;
	connection->external_references++;
	return true;
}

bool
tcp_core_connection_release(struct tcp_connection_table *table,
	struct tcp_connection_handle handle)
{
	struct tcp_core_connection *connection = connection_lookup(table, handle);
	if (connection == NULL || connection->external_references == 0)
		return false;
	connection->external_references--;
	connection_collect(connection);
	return true;
}

bool
tcp_core_connection_get_tuple(const struct tcp_connection_table *table,
	struct tcp_connection_handle handle, struct tcp_core_tuple *tuple)
{
	const struct tcp_core_connection *connection =
	    connection_lookup_const(table, handle);
	if (connection == NULL || tuple == NULL)
		return false;
	*tuple = connection->tuple;
	return true;
}

static uint16_t
receive_window(const struct tcp_core_connection *connection)
{
	return (uint16_t)(TCP_CORE_RX_CAPACITY - connection->rx_length);
}

static void
fill_result(const struct tcp_core_connection *connection,
	struct tcp_core_result *result)
{
	result->state = connection->state;
	result->snd_una = connection->snd_una;
	result->snd_nxt = connection->snd_nxt;
	result->rcv_nxt = connection->rcv_nxt;
	result->send_window = connection->snd_wnd;
	result->receive_window = receive_window(connection);
	result->timer_armed = connection->timer_armed;
	result->timer_deadline = connection->retransmit_deadline;
	result->timer_interval = connection->retransmit_interval;
	result->retransmissions = connection->retransmissions;
}

bool
tcp_core_connection_get(const struct tcp_connection_table *table,
	struct tcp_connection_handle handle, struct tcp_core_result *snapshot)
{
	const struct tcp_core_connection *connection =
	    connection_lookup_const(table, handle);
	if (connection == NULL || snapshot == NULL)
		return false;
	*snapshot = (struct tcp_core_result){0};
	fill_result(connection, snapshot);
	return true;
}

size_t
tcp_core_connection_count(const struct tcp_connection_table *table)
{
	size_t count = 0;
	if (table == NULL)
		return 0;
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++)
		count += table->slots[i].allocated ? 1U : 0U;
	return count;
}

bool
tcp_core_connection_read(struct tcp_connection_table *table,
	struct tcp_connection_handle handle, uint8_t *data, size_t capacity,
	size_t *length)
{
	struct tcp_core_connection *connection = connection_lookup(table, handle);
	size_t amount;
	if (connection == NULL || length == NULL ||
	    (capacity != 0 && data == NULL))
		return false;
	amount = capacity < connection->rx_length ? capacity : connection->rx_length;
	for (size_t i = 0; i < amount; i++)
		data[i] = connection->rx_data[(connection->rx_head + i) %
		    TCP_CORE_RX_CAPACITY];
	connection->rx_head = (connection->rx_head + amount) %
	    TCP_CORE_RX_CAPACITY;
	connection->rx_length -= amount;
	if (connection->rx_length == 0)
		connection->rx_head = 0;
	*length = amount;
	return true;
}

static bool
sequence_between(uint32_t value, uint32_t low, uint32_t high)
{
	uint32_t span = high - low;
	uint32_t offset = value - low;
	return span < 0x80000000U && offset <= span;
}

/* TCP serial ordering is meaningful for distances below half the sequence
 * space; supported windows and outstanding spans stay within that bound. */
static bool
sequence_after(uint32_t left, uint32_t right)
{
	uint32_t distance = left - right;
	return distance != 0 && distance < 0x80000000U;
}

static bool
sequence_in_window(uint32_t sequence, uint32_t next, uint16_t window)
{
	return window != 0 && (uint32_t)(sequence - next) < window;
}

/* Scheduled intervals are far below half the uint64 tick range. */
static bool
tick_reached(uint64_t now, uint64_t deadline)
{
	return now - deadline < (UINT64_C(1) << 63);
}

static bool
input_matches(const struct tcp_core_connection *connection,
	const struct tcp_core_event *event)
{
	const struct tcp_segment_view *segment = event->segment;
	if (segment == NULL ||
	    segment->destination_port != connection->tuple.local_port)
		return false;
	if (connection->state == TCP_STATE_LISTEN) {
		return (connection->tuple.local_address[0] == 0 &&
		    connection->tuple.local_address[1] == 0 &&
		    connection->tuple.local_address[2] == 0 &&
		    connection->tuple.local_address[3] == 0) ||
		    address_equal(event->destination_address,
		    connection->tuple.local_address);
	}
	return segment->source_port == connection->tuple.remote_port &&
	    address_equal(event->source_address, connection->tuple.remote_address) &&
	    address_equal(event->destination_address, connection->tuple.local_address);
}

static bool
segment_view_valid(const struct tcp_segment_view *segment)
{
	if (segment == NULL || segment->source_port == 0 ||
	    segment->destination_port == 0 ||
	    (segment->flags & ~TCP_SUPPORTED_FLAGS) != 0 ||
	    segment->payload_length > TCP_CORE_MAX_SEGMENT ||
	    (segment->payload_length != 0 && segment->payload == NULL) ||
	    (segment->mss_present &&
	    ((segment->flags & TCP_FLAG_SYN) == 0 || segment->mss == 0)))
		return false;
	return true;
}

static bool
segment_sequence_acceptable(const struct tcp_core_connection *connection,
	const struct tcp_segment_view *segment)
{
	uint32_t span = (uint32_t)segment->payload_length +
	    ((segment->flags & TCP_FLAG_FIN) != 0 ? 1U : 0U) +
	    ((segment->flags & TCP_FLAG_SYN) != 0 ? 1U : 0U);
	uint16_t window = receive_window(connection);
	if (span == 0)
		return segment->sequence == connection->rcv_nxt;
	if (window == 0 || span > window)
		return false;
	return segment->sequence == connection->rcv_nxt ||
	    (sequence_in_window(segment->sequence, connection->rcv_nxt, window) &&
	    (uint32_t)(segment->sequence - connection->rcv_nxt) + span <= window);
}

static void
activate_connection(struct tcp_core_connection *connection)
{
	connection->table_owned = true;
}

static void
close_connection(struct tcp_core_connection *connection,
	struct tcp_core_result *result, bool aborted)
{
	connection->state = TCP_STATE_CLOSED;
	connection->outstanding_control = false;
	connection->timer_armed = false;
	connection->rx_head = 0;
	connection->rx_length = 0;
	if (aborted)
		result->actions |= TCP_ACTION_ABORT;
	table_reference_drop(connection);
}

static void
arm_control_timer(struct tcp_core_connection *connection, uint64_t now_tick)
{
	connection->timer_armed = true;
	connection->retransmissions = 0;
	connection->retransmit_interval = TCP_CORE_RTO_INITIAL_TICKS;
	connection->retransmit_deadline = now_tick +
	    connection->retransmit_interval;
}

static bool
acknowledgment_valid(const struct tcp_core_connection *connection,
	const struct tcp_segment_view *segment)
{
	return (segment->flags & TCP_FLAG_ACK) == 0 ||
	    sequence_between(segment->acknowledgment, connection->snd_una,
	    connection->snd_nxt);
}

static void
accept_ack(struct tcp_core_connection *connection,
	const struct tcp_segment_view *segment)
{
	if ((segment->flags & TCP_FLAG_ACK) == 0)
		return;
	if (sequence_after(segment->acknowledgment, connection->snd_una) &&
	    sequence_between(segment->acknowledgment, connection->snd_una,
	    connection->snd_nxt)) {
		uint32_t old_una = connection->snd_una;
		bool acknowledges_control = connection->outstanding_control &&
		    sequence_between(connection->outstanding_end, old_una,
		    segment->acknowledgment);
		connection->snd_una = segment->acknowledgment;
		connection->duplicate_acks = 0;
		connection->snd_wnd = segment->window < TCP_CORE_SEND_WINDOW_LIMIT ?
		    segment->window : TCP_CORE_SEND_WINDOW_LIMIT;
		if (acknowledges_control) {
			connection->outstanding_control = false;
			connection->timer_armed = false;
		}
	} else if (segment->acknowledgment == connection->snd_una &&
	    connection->outstanding_control &&
	    connection->duplicate_acks < TCP_CORE_RETRY_LIMIT) {
		connection->duplicate_acks++;
	}
}

static void
enter_time_wait(struct tcp_core_connection *connection,
	const struct tcp_core_event *event, struct tcp_core_result *result)
{
	connection->state = TCP_STATE_TIME_WAIT;
	connection->timer_armed = false;
	connection->time_wait_deadline = event->now_tick + TCP_CORE_TIME_WAIT_TICKS;
	result->actions |= TCP_ACTION_ENTER_TIME_WAIT | TCP_ACTION_SEND_ACK;
}

#ifdef TCP_CORE_TESTING
static bool
process_listener_segment(struct tcp_connection_table *table,
	struct tcp_core_connection *listener, const struct tcp_core_event *event,
	struct tcp_core_result *result)
{
	const struct tcp_segment_view *segment = event->segment;
	struct tcp_core_tuple child_tuple = listener->tuple;
	struct tcp_core_connection *child;
	size_t pending = 0;
	uint8_t listener_slot = (uint8_t)(listener - table->slots);
	if ((segment->flags & TCP_FLAG_RST) != 0)
		return true;
	if ((segment->flags & TCP_FLAG_ACK) != 0) {
		result->actions |= TCP_ACTION_SEND_RST;
		return true;
	}
	if (segment->flags != TCP_FLAG_SYN || segment->payload_length != 0 ||
	    segment->source_port == 0)
		return true;
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++)
		pending += table->slots[i].allocated &&
		    table->slots[i].state == TCP_STATE_SYN_RECEIVED &&
		    table->slots[i].has_listener &&
		    table->slots[i].listener_slot == listener_slot &&
		    table->slots[i].listener_generation == listener->generation ?
		    1U : 0U;
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++) {
		struct tcp_core_connection *existing = &table->slots[i];
		if (!existing->allocated || existing->state != TCP_STATE_SYN_RECEIVED ||
		    !existing->has_listener || existing->listener_slot != listener_slot ||
		    existing->listener_generation != listener->generation ||
		    existing->tuple.remote_port != segment->source_port ||
		    !address_equal(existing->tuple.local_address,
		    event->destination_address) ||
		    !address_equal(existing->tuple.remote_address,
		    event->source_address) || existing->irs != segment->sequence)
			continue;
		result->actions |= TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN |
		    TCP_ACTION_SEND_ACK;
		return true;
	}
	if (pending >= TCP_CORE_LISTEN_BACKLOG) {
		result->actions |= TCP_ACTION_SEND_RST;
		return true;
	}
	for (size_t i = 0; i < 4; i++) {
		child_tuple.local_address[i] = event->destination_address[i];
		child_tuple.remote_address[i] = event->source_address[i];
	}
	child_tuple.remote_port = segment->source_port;
	if (!connection_allocate(table, &child_tuple, &result->child, &child)) {
		result->actions |= TCP_ACTION_SEND_RST;
		return true;
	}
	child->state = TCP_STATE_SYN_RECEIVED;
	child->started = true;
	child->iss = event->initial_sequence;
	child->irs = segment->sequence;
	child->snd_una = child->iss;
	child->snd_nxt = child->iss + 1;
	child->outstanding_end = child->snd_nxt;
	child->rcv_nxt = child->irs + 1;
	child->snd_wnd = segment->window < TCP_CORE_SEND_WINDOW_LIMIT ?
	    segment->window : TCP_CORE_SEND_WINDOW_LIMIT;
	child->peer_mss = segment->mss_present &&
	    segment->mss < TCP_CORE_LOCAL_MSS ? segment->mss :
	    (segment->mss_present ? TCP_CORE_LOCAL_MSS : TCP_CORE_DEFAULT_MSS);
	child->outstanding_control = true;
	child->outstanding_flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
	child->has_listener = true;
	child->listener_slot = listener_slot;
	child->listener_generation = listener->generation;
	child->table_owned = true;
	arm_control_timer(child, event->now_tick);
	result->has_child = true;
	result->actions |= TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK;
	return true;
}
#endif

static bool
process_syn_sent(struct tcp_core_connection *connection,
	const struct tcp_segment_view *segment, struct tcp_core_result *result)
{
	if ((segment->flags & TCP_FLAG_ACK) != 0 &&
	    (!sequence_after(segment->acknowledgment, connection->iss) ||
	    !sequence_between(segment->acknowledgment, connection->iss,
	    connection->snd_nxt))) {
		if ((segment->flags & TCP_FLAG_RST) == 0)
			result->actions |= TCP_ACTION_SEND_RST;
		return true;
	}
	if ((segment->flags & TCP_FLAG_RST) != 0) {
		if (segment->flags == (TCP_FLAG_RST | TCP_FLAG_ACK) &&
		    segment->acknowledgment == connection->snd_nxt)
			close_connection(connection, result, true);
		return true;
	}
	if ((segment->flags & TCP_FLAG_SYN) == 0 ||
	    (segment->flags != TCP_FLAG_SYN &&
	    segment->flags != (TCP_FLAG_SYN | TCP_FLAG_ACK)))
		return true;
	if (segment->payload_length != 0 ||
	    ((segment->flags & TCP_FLAG_ACK) != 0 &&
	    segment->acknowledgment != connection->snd_nxt))
		return true;
	connection->irs = segment->sequence;
	connection->rcv_nxt = segment->sequence + 1;
	if ((segment->flags & TCP_FLAG_ACK) != 0) {
		connection->snd_una = segment->acknowledgment;
		connection->state = TCP_STATE_ESTABLISHED;
		connection->outstanding_control = false;
		connection->timer_armed = false;
		connection->snd_wnd = segment->window < TCP_CORE_SEND_WINDOW_LIMIT ?
		    segment->window : TCP_CORE_SEND_WINDOW_LIMIT;
		result->actions |= TCP_ACTION_SEND_ACK;
	} else {
		connection->state = TCP_STATE_SYN_RECEIVED;
		connection->outstanding_flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
		connection->snd_wnd = segment->window < TCP_CORE_SEND_WINDOW_LIMIT ?
		    segment->window : TCP_CORE_SEND_WINDOW_LIMIT;
		result->actions |= TCP_ACTION_SEND_SYN | TCP_ACTION_SEND_ACK;
	}
	if (segment->mss_present)
		connection->peer_mss = segment->mss < TCP_CORE_LOCAL_MSS ?
		    segment->mss : TCP_CORE_LOCAL_MSS;
	return true;
}

static bool
process_syn_received(struct tcp_core_connection *connection,
	const struct tcp_segment_view *segment, struct tcp_core_result *result)
{
	if ((segment->flags & TCP_FLAG_RST) != 0) {
		if ((segment->flags == TCP_FLAG_RST ||
		    segment->flags == (TCP_FLAG_RST | TCP_FLAG_ACK)) &&
		    segment->sequence == connection->rcv_nxt)
			close_connection(connection, result, true);
		return true;
	}
	if (segment->flags == TCP_FLAG_SYN &&
	    segment->sequence == connection->irs) {
		result->actions |= TCP_ACTION_RETRANSMIT | TCP_ACTION_SEND_SYN |
		    TCP_ACTION_SEND_ACK;
		return true;
	}
	if ((segment->flags & TCP_FLAG_ACK) != 0 &&
	    segment->acknowledgment != connection->snd_nxt) {
		result->actions |= TCP_ACTION_SEND_RST;
		return true;
	}
	/* Active SYN-RECEIVED is the simultaneous-open path. The peer's
	 * SYN-ACK reuses its original SYN sequence and acknowledges our SYN. */
	if (!connection->has_listener &&
	    segment->flags == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
	    segment->sequence == connection->irs &&
	    segment->acknowledgment == connection->snd_nxt &&
	    segment->payload_length == 0) {
		connection->snd_una = segment->acknowledgment;
		connection->snd_wnd = segment->window < TCP_CORE_SEND_WINDOW_LIMIT ?
		    segment->window : TCP_CORE_SEND_WINDOW_LIMIT;
		connection->outstanding_control = false;
		connection->timer_armed = false;
		connection->state = TCP_STATE_ESTABLISHED;
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	if (segment->flags != TCP_FLAG_ACK || segment->payload_length != 0)
		return true;
	if (segment->sequence != connection->rcv_nxt) {
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	connection->snd_una = segment->acknowledgment;
	connection->snd_wnd = segment->window < TCP_CORE_SEND_WINDOW_LIMIT ?
	    segment->window : TCP_CORE_SEND_WINDOW_LIMIT;
	connection->outstanding_control = false;
	connection->timer_armed = false;
	connection->state = TCP_STATE_ESTABLISHED;
	return true;
}

static bool
process_established_segment(struct tcp_core_connection *connection,
	const struct tcp_core_event *event, const struct tcp_segment_view *segment,
	struct tcp_core_result *result)
{
	uint16_t window = receive_window(connection);
	bool has_sequence_space = segment->payload_length != 0 ||
	    (segment->flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) != 0;
	uint32_t sequence_span = (uint32_t)segment->payload_length +
	    ((segment->flags & TCP_FLAG_FIN) != 0 ? 1U : 0U);
	if ((segment->flags & TCP_FLAG_RST) != 0) {
		if ((window == 0 && segment->sequence == connection->rcv_nxt) ||
		    sequence_in_window(segment->sequence, connection->rcv_nxt, window))
			close_connection(connection, result, true);
		return true;
	}
	if ((segment->flags & TCP_FLAG_SYN) != 0) {
		if (segment->sequence == connection->irs &&
		    (segment->flags & TCP_FLAG_ACK) != 0 &&
		    segment->acknowledgment == connection->snd_una)
			result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	if (!segment_sequence_acceptable(connection, segment)) {
		result->actions |= TCP_ACTION_SEND_ACK;
		if ((segment->payload_length != 0 ||
		    (segment->flags & TCP_FLAG_FIN) != 0) &&
		    sequence_in_window(segment->sequence, connection->rcv_nxt,
		    window))
			result->actions |= TCP_ACTION_DUPLICATE_ACK;
		return true;
	}
	if ((segment->flags & TCP_FLAG_ACK) != 0 &&
	    !sequence_between(segment->acknowledgment, connection->snd_una,
	    connection->snd_nxt) &&
	    !sequence_after(connection->snd_una, segment->acknowledgment)) {
		/* A future/serially ambiguous ACK cannot acknowledge unsent data. */
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	accept_ack(connection, segment);
	if ((connection->state == TCP_STATE_CLOSE_WAIT ||
	    connection->state == TCP_STATE_LAST_ACK ||
	    connection->state == TCP_STATE_CLOSING) &&
	    segment->payload_length != 0) {
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	if (has_sequence_space && segment->sequence != connection->rcv_nxt) {
		result->actions |= TCP_ACTION_SEND_ACK;
		if (sequence_in_window(segment->sequence, connection->rcv_nxt,
		    window))
			result->actions |= TCP_ACTION_DUPLICATE_ACK;
		return true;
	}
	if (!has_sequence_space && segment->sequence != connection->rcv_nxt) {
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	if (sequence_span > window || segment->payload_length > window ||
	    segment->payload_length > TCP_CORE_RX_CAPACITY - connection->rx_length) {
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	if (segment->payload_length != 0) {
		for (size_t i = 0; i < segment->payload_length; i++)
			connection->rx_data[(connection->rx_head + connection->rx_length + i) %
			    TCP_CORE_RX_CAPACITY] = segment->payload[i];
		connection->rx_length += segment->payload_length;
		connection->rcv_nxt += (uint32_t)segment->payload_length;
		result->accepted_bytes = segment->payload_length;
		result->actions |= TCP_ACTION_DELIVER_DATA | TCP_ACTION_SEND_ACK;
	}
	if ((segment->flags & TCP_FLAG_FIN) == 0)
		return true;
	if (connection->state == TCP_STATE_CLOSE_WAIT ||
	    connection->state == TCP_STATE_LAST_ACK ||
	    connection->state == TCP_STATE_CLOSING) {
		result->actions |= TCP_ACTION_SEND_ACK;
		return true;
	}
	connection->rcv_nxt++;
	result->actions |= TCP_ACTION_SEND_ACK;
	if (connection->state == TCP_STATE_FIN_WAIT_1) {
		if (!connection->outstanding_control)
			enter_time_wait(connection, event, result);
		else
			connection->state = TCP_STATE_CLOSING;
	} else if (connection->state == TCP_STATE_FIN_WAIT_2) {
		enter_time_wait(connection, event, result);
	} else if (connection->state == TCP_STATE_ESTABLISHED) {
		connection->state = TCP_STATE_CLOSE_WAIT;
	}
	return true;
}

static void
fill_result_if_live(struct tcp_core_connection *connection,
	struct tcp_core_result *result)
{
	if (connection->allocated)
		fill_result(connection, result);
	else
		result->state = TCP_STATE_CLOSED;
}

bool
tcp_core_step(struct tcp_connection_table *table,
	struct tcp_connection_handle handle, const struct tcp_core_event *event,
	struct tcp_core_result *result)
{
	struct tcp_core_connection *connection = connection_lookup(table, handle);
	if (connection == NULL || event == NULL || result == NULL)
		return false;
	*result = (struct tcp_core_result){0};
	switch (event->type) {
	case TCP_EVENT_LISTEN:
		if (connection->state != TCP_STATE_CLOSED || connection->started ||
		    connection->tuple.remote_port != 0)
			return false;
		connection->started = true;
		connection->state = TCP_STATE_LISTEN;
		activate_connection(connection);
		break;
#ifdef TCP_CORE_TESTING
	case TCP_EVENT_ACTIVE_OPEN:
		if (connection->state != TCP_STATE_CLOSED || connection->started ||
		    connection->tuple.remote_port == 0)
			return false;
		connection->started = true;
		connection->iss = event->initial_sequence;
		connection->snd_una = event->initial_sequence;
		connection->snd_nxt = event->initial_sequence + 1;
		connection->outstanding_end = connection->snd_nxt;
		connection->state = TCP_STATE_SYN_SENT;
		connection->outstanding_control = true;
		connection->outstanding_flags = TCP_FLAG_SYN;
		activate_connection(connection);
		arm_control_timer(connection, event->now_tick);
		result->actions |= TCP_ACTION_SEND_SYN;
		break;
#endif
	case TCP_EVENT_SEGMENT:
		{
		bool fin_acknowledged;
		if (!segment_view_valid(event->segment) ||
		    !input_matches(connection, event))
			return false;
		if (connection->state == TCP_STATE_LISTEN) {
#ifdef TCP_CORE_TESTING
			if (!process_listener_segment(table, connection, event, result))
				return false;
#else
			return false;
#endif
			break;
		}
		if (connection->state == TCP_STATE_SYN_SENT) {
			(void)process_syn_sent(connection, event->segment, result);
			break;
		}
		if (connection->state == TCP_STATE_SYN_RECEIVED) {
			(void)process_syn_received(connection, event->segment, result);
			break;
		}
		if (connection->state == TCP_STATE_CLOSED)
			return true;
		if (connection->state == TCP_STATE_TIME_WAIT) {
			if ((event->segment->flags & TCP_FLAG_FIN) != 0 &&
			    event->segment->sequence +
			    (uint32_t)event->segment->payload_length + 1U ==
			    connection->rcv_nxt) {
				result->actions |= TCP_ACTION_SEND_ACK;
				connection->time_wait_deadline = event->now_tick +
				    TCP_CORE_TIME_WAIT_TICKS;
			}
			break;
		}
		fin_acknowledged = connection->outstanding_control &&
		    (connection->outstanding_flags & TCP_FLAG_FIN) != 0 &&
		    (event->segment->flags & TCP_FLAG_ACK) != 0 &&
		    acknowledgment_valid(connection, event->segment) &&
		    segment_sequence_acceptable(connection, event->segment) &&
		    event->segment->acknowledgment == connection->outstanding_end;
		(void)process_established_segment(connection, event,
		    event->segment, result);
		if (fin_acknowledged) {
			connection->outstanding_control = false;
			if (connection->state == TCP_STATE_FIN_WAIT_1)
				connection->state = TCP_STATE_FIN_WAIT_2;
			else if (connection->state == TCP_STATE_CLOSING)
				enter_time_wait(connection, event, result);
			else if (connection->state == TCP_STATE_LAST_ACK)
				close_connection(connection, result, false);
		}
		break;
		}
	case TCP_EVENT_APPLICATION_CLOSE:
		if (connection->state == TCP_STATE_LISTEN) {
			connection->state = TCP_STATE_CLOSED;
			table_reference_drop(connection);
		} else if (connection->state == TCP_STATE_SYN_SENT ||
		    connection->state == TCP_STATE_SYN_RECEIVED) {
			result->actions |= TCP_ACTION_SEND_RST;
			close_connection(connection, result, true);
		} else if (connection->state == TCP_STATE_ESTABLISHED ||
		    connection->state == TCP_STATE_CLOSE_WAIT) {
			connection->outstanding_control = true;
			connection->outstanding_flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
			connection->snd_nxt++;
			connection->outstanding_end = connection->snd_nxt;
			connection->state = connection->state == TCP_STATE_ESTABLISHED ?
			    TCP_STATE_FIN_WAIT_1 : TCP_STATE_LAST_ACK;
			arm_control_timer(connection, event->now_tick);
			result->actions |= TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK;
		} else if (connection->state != TCP_STATE_CLOSED &&
		    connection->state != TCP_STATE_TIME_WAIT) {
			/* Repeated close is idempotent while a FIN is outstanding. */
		}
		break;
	case TCP_EVENT_RETRANSMIT_TIMEOUT:
		if (!connection->outstanding_control || !connection->timer_armed ||
		    !tick_reached(event->now_tick, connection->retransmit_deadline))
			break;
		if (connection->retransmissions >= TCP_CORE_RETRY_LIMIT) {
			close_connection(connection, result, true);
			break;
		}
		connection->retransmissions++;
		if (connection->retransmit_interval < TCP_CORE_RTO_MAX_TICKS) {
			connection->retransmit_interval *= 2;
			if (connection->retransmit_interval > TCP_CORE_RTO_MAX_TICKS)
				connection->retransmit_interval = TCP_CORE_RTO_MAX_TICKS;
		}
		connection->retransmit_deadline = event->now_tick +
		    connection->retransmit_interval;
		result->actions |= TCP_ACTION_RETRANSMIT;
		if ((connection->outstanding_flags & TCP_FLAG_SYN) != 0)
			result->actions |= TCP_ACTION_SEND_SYN;
		if ((connection->outstanding_flags & TCP_FLAG_ACK) != 0)
			result->actions |= TCP_ACTION_SEND_ACK;
		if ((connection->outstanding_flags & TCP_FLAG_FIN) != 0)
			result->actions |= TCP_ACTION_SEND_FIN;
		break;
	case TCP_EVENT_TIME_WAIT_EXPIRE:
		if (connection->state == TCP_STATE_TIME_WAIT &&
		    tick_reached(event->now_tick, connection->time_wait_deadline)) {
			connection->state = TCP_STATE_CLOSED;
			table_reference_drop(connection);
		}
		break;
	default:
		return false;
	}
	fill_result_if_live(connection, result);
	return true;
}

static bool
connection_timer_due(const struct tcp_core_connection *connection,
	uint64_t now_tick)
{
	if (!connection->allocated)
		return false;
	if (connection->timer_armed &&
	    tick_reached(now_tick, connection->retransmit_deadline))
		return true;
	return connection->state == TCP_STATE_TIME_WAIT &&
	    tick_reached(now_tick, connection->time_wait_deadline);
}

bool
tcp_core_service(struct tcp_connection_table *table, uint64_t now_tick,
	size_t budget, struct tcp_core_service_result *result)
{
	if (table == NULL || result == NULL ||
	    budget > TCP_CORE_SERVICE_EVENT_LIMIT)
		return false;
	*result = (struct tcp_core_service_result){0};
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT &&
	    result->count < budget; i++) {
		struct tcp_core_connection *connection = &table->slots[i];
		struct tcp_connection_handle handle;
		struct tcp_core_event event;
		struct tcp_core_service_item *item;
		if (!connection_timer_due(connection, now_tick) ||
		    connection->external_references == UINT16_MAX)
			continue;
		handle = (struct tcp_connection_handle){
			.slot = (uint8_t)i,
			.generation = connection->generation
		};
		/* The returned item owns this reference, including if timeout closes
		 * the table's reference while the result is being consumed. */
		connection->external_references++;
		event = (struct tcp_core_event){
			.type = connection->state == TCP_STATE_TIME_WAIT ?
			    TCP_EVENT_TIME_WAIT_EXPIRE : TCP_EVENT_RETRANSMIT_TIMEOUT,
			.now_tick = now_tick
		};
		item = &result->items[result->count];
		item->handle = handle;
		if (!tcp_core_step(table, handle, &event, &item->result)) {
			(void)tcp_core_connection_release(table, handle);
			for (size_t j = 0; j < result->count; j++)
				(void)tcp_core_connection_release(table,
				    result->items[j].handle);
			*result = (struct tcp_core_service_result){0};
			return false;
		}
		result->count++;
	}
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++)
		if (connection_timer_due(&table->slots[i], now_tick)) {
			result->more_due = true;
			break;
		}
	return true;
}

bool
tcp_core_live_isn_available(void)
{
	return false;
}
