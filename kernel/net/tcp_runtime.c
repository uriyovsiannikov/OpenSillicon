#include "kernel/tcp_runtime.h"

enum tcp_runtime_state {
	TCP_RUNTIME_UNINITIALIZED,
	TCP_RUNTIME_READY,
	TCP_RUNTIME_STOPPING,
	TCP_RUNTIME_STOPPED
};

static struct tcp_connection_table connection_table;
static enum tcp_runtime_state runtime_state;
static bool timer_service_active;

#ifdef TCP_RUNTIME_TESTING
struct runtime_child_link {
	bool occupied;
	struct tcp_connection_handle child;
	struct tcp_connection_handle listener;
};

/* The core table remains the sole slot allocator. This bounded side table
 * records runtime ownership and the parent listener for each passive child. */
static struct runtime_child_link child_links[TCP_RUNTIME_CONNECTION_LIMIT];
#endif

static uint64_t
runtime_guard_enter(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
#else
	return 0;
#endif
}

static void
runtime_guard_leave(uint64_t flags)
{
#ifdef OPEN_SILLICON_KERNEL
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
#else
	(void)flags;
#endif
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
address_zero(const uint8_t address[4])
{
	static const uint8_t zero[4] = {0};
	return address_equal(address, zero);
}

static bool
address_unicast(const uint8_t address[4])
{
	return !address_zero(address) && address[0] < 224 &&
	    !(address[0] == 255 && address[1] == 255 &&
	    address[2] == 255 && address[3] == 255);
}

#ifdef TCP_RUNTIME_TESTING
static bool parsed_segment_valid(const struct tcp_wire_segment *segment);
#endif

static bool
handle_release_locked(struct tcp_connection_handle handle)
{
	return (runtime_state == TCP_RUNTIME_READY ||
	    runtime_state == TCP_RUNTIME_STOPPING) &&
	    tcp_core_connection_release(&connection_table, handle);
}

bool
tcp_runtime_init(void)
{
	uint64_t flags = runtime_guard_enter();
	bool success = runtime_state == TCP_RUNTIME_UNINITIALIZED;
	if (success) {
		tcp_core_table_init(&connection_table);
#ifdef TCP_RUNTIME_TESTING
		for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++)
			child_links[i] = (struct runtime_child_link){0};
#endif
		timer_service_active = false;
		runtime_state = TCP_RUNTIME_READY;
	}
	runtime_guard_leave(flags);
	return success;
}

#ifdef TCP_RUNTIME_TESTING
static bool
same_handle(struct tcp_connection_handle a, struct tcp_connection_handle b)
{
	return a.slot == b.slot && a.generation == b.generation;
}

static struct runtime_child_link *
child_link_find(struct tcp_connection_handle child)
{
	if (child.slot >= TCP_RUNTIME_CONNECTION_LIMIT)
		return NULL;
	struct runtime_child_link *link = &child_links[child.slot];
	return link->occupied && same_handle(link->child, child) ? link : NULL;
}

static bool
listener_has_children(struct tcp_connection_handle listener)
{
	for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++)
		if (child_links[i].occupied &&
		    same_handle(child_links[i].listener, listener))
			return true;
	return false;
}

static size_t
listener_pending_children(struct tcp_connection_handle listener)
{
	size_t pending = 0;
	for (size_t i = 0; i < TCP_RUNTIME_CONNECTION_LIMIT; i++) {
		struct tcp_core_result snapshot;
		if (child_links[i].occupied &&
		    same_handle(child_links[i].listener, listener) &&
		    tcp_core_connection_get(&connection_table,
		    child_links[i].child, &snapshot) &&
		    snapshot.state == TCP_STATE_SYN_RECEIVED)
			pending++;
	}
	return pending;
}

static bool
child_link_drop(struct runtime_child_link *link)
{
	struct tcp_connection_handle child = link->child;
	struct tcp_connection_handle listener = link->listener;
	struct tcp_core_result snapshot;
	/* The relationship owns one reference to each endpoint. Validate both
	 * generations first. The listener is table-owned while a link exists, so
	 * its reference can be restored if child release unexpectedly fails. */
	if (!tcp_core_connection_get(&connection_table, child, &snapshot) ||
	    !tcp_core_connection_get(&connection_table, listener, &snapshot) ||
	    !tcp_core_connection_release(&connection_table, listener))
		return false;
	if (!tcp_core_connection_release(&connection_table, child)) {
		(void)tcp_core_connection_retain(&connection_table, listener);
		return false;
	}
	*link = (struct runtime_child_link){0};
	return true;
}
#endif

static bool
runtime_state_terminal(enum tcp_connection_state state)
{
	/* CLOSED is the only terminal Core state. TIME_WAIT and all FIN states
	 * retain protocol/table ownership until their specific event completes. */
	return state == TCP_STATE_CLOSED;
}

static bool
runtime_terminal_cleanup(struct tcp_connection_handle handle,
	enum tcp_connection_state state)
{
	if (!runtime_state_terminal(state))
		return true;
#ifdef TCP_RUNTIME_TESTING
	{
		struct runtime_child_link *link = child_link_find(handle);
		return link == NULL || child_link_drop(link);
	}
#else
	(void)handle;
	return true;
#endif
}

bool
tcp_runtime_shutdown(void)
{
	uint64_t flags = runtime_guard_enter();
	bool success = false;
	if (runtime_state == TCP_RUNTIME_UNINITIALIZED ||
	    runtime_state == TCP_RUNTIME_STOPPED)
		goto out;
	if (timer_service_active)
		goto out;
	/* STOPPING is sticky; new listeners and route work are refused while
	 * outstanding listener/table/route references are drained explicitly. */
	runtime_state = TCP_RUNTIME_STOPPING;
	if (tcp_core_connection_count(&connection_table) != 0)
		goto out;
	runtime_state = TCP_RUNTIME_STOPPED;
	success = true;
out:
	runtime_guard_leave(flags);
	return success;
}

static enum tcp_runtime_timer_status
runtime_service_timers_at(uint64_t now_tick, size_t budget,
	struct tcp_runtime_timer_result *result)
{
	struct tcp_core_service_result core_result;
	uint64_t flags;
	enum tcp_runtime_timer_status status;
	if (result == NULL)
		return TCP_RUNTIME_TIMER_INVALID;
	*result = (struct tcp_runtime_timer_result){
		.status = TCP_RUNTIME_TIMER_INVALID
	};
	if (budget > TCP_RUNTIME_TIMER_EVENT_LIMIT)
		return TCP_RUNTIME_TIMER_INVALID;
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_TIMER_STOPPING;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY) {
		status = TCP_RUNTIME_TIMER_DISABLED;
		goto out;
	}
	if (timer_service_active) {
		status = TCP_RUNTIME_TIMER_BUSY;
		goto out;
	}
	timer_service_active = true;
	if (!tcp_core_service(&connection_table, now_tick, budget, &core_result)) {
		timer_service_active = false;
		status = TCP_RUNTIME_TIMER_CORE_ERROR;
		goto out;
	}
	result->count = core_result.count;
	result->more_due = core_result.more_due;
	for (size_t i = 0; i < core_result.count; i++) {
		struct tcp_core_service_item *item = &core_result.items[i];
		struct tcp_core_result snapshot;
		struct tcp_connection_handle handle = item->handle;
		if (handle.slot >= TCP_RUNTIME_CONNECTION_LIMIT ||
		    !tcp_core_connection_get(&connection_table, handle, &snapshot) ||
		    snapshot.state != item->result.state) {
			/* The core contract guarantees a retained, generation-valid handle.
			 * If that invariant fails, attempt exactly one checked release and
			 * release all later valid items before reporting the fault. */
			(void)tcp_core_connection_release(&connection_table, handle);
			for (size_t j = i + 1; j < core_result.count; j++)
				(void)tcp_core_connection_release(&connection_table,
				    core_result.items[j].handle);
			result->count = 0;
			result->more_due = core_result.more_due;
			timer_service_active = false;
			status = TCP_RUNTIME_TIMER_STALE_HANDLE;
			goto out;
		}
		result->items[i] = (struct tcp_runtime_timer_item){
			.actions = item->result.actions,
			.state = item->result.state,
			.snd_una = item->result.snd_una,
			.snd_nxt = item->result.snd_nxt,
			.rcv_nxt = item->result.rcv_nxt,
			.send_window = item->result.send_window,
			.receive_window = item->result.receive_window,
			.timer_armed = item->result.timer_armed,
			.accepted_bytes = item->result.accepted_bytes,
			.has_child = item->result.has_child,
			.timer_deadline = item->result.timer_deadline,
			.timer_interval = item->result.timer_interval,
			.retransmissions = item->result.retransmissions
		};
#ifdef TCP_RUNTIME_TESTING
		if (runtime_state_terminal(item->result.state)) {
			if (!runtime_terminal_cleanup(handle, item->result.state)) {
				(void)tcp_core_connection_release(&connection_table,
				    handle);
				for (size_t j = i + 1; j < core_result.count; j++)
					(void)tcp_core_connection_release(&connection_table,
					    core_result.items[j].handle);
				result->count = 0;
				timer_service_active = false;
				status = TCP_RUNTIME_TIMER_CORE_ERROR;
				goto out;
			}
		}
#endif
		/* Copy all scalar data and retire runtime child ownership before this
		 * service-owned reference can be the last reference to the slot. */
		if (!tcp_core_connection_release(&connection_table, handle)) {
			for (size_t j = i + 1; j < core_result.count; j++)
				(void)tcp_core_connection_release(&connection_table,
				    core_result.items[j].handle);
			result->count = 0;
			timer_service_active = false;
			status = TCP_RUNTIME_TIMER_CORE_ERROR;
			goto out;
		}
	}
	timer_service_active = false;
	status = TCP_RUNTIME_TIMER_OK;
out:
	result->status = status;
	runtime_guard_leave(flags);
	return status;
}

enum tcp_runtime_timer_status
tcp_runtime_service_timers(uint64_t now_tick, size_t budget,
	struct tcp_runtime_timer_result *result)
{
	return runtime_service_timers_at(now_tick, budget, result);
}

bool
tcp_runtime_ready(void)
{
	uint64_t flags = runtime_guard_enter();
	bool ready = runtime_state == TCP_RUNTIME_READY;
	runtime_guard_leave(flags);
	return ready;
}

enum tcp_runtime_listener_status
tcp_runtime_listener_open(const uint8_t local_address[4], uint16_t local_port,
	struct tcp_connection_handle *handle)
{
	struct tcp_core_tuple tuple = {0};
	struct tcp_connection_handle created;
	struct tcp_core_event event = {.type = TCP_EVENT_LISTEN};
	struct tcp_core_result result;
	uint64_t flags;
	enum tcp_runtime_listener_status status;
	if (local_address == NULL || handle == NULL || local_port == 0 ||
	    (!address_zero(local_address) && !address_unicast(local_address)))
		return TCP_RUNTIME_LISTENER_INVALID;
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_LISTENER_STOPPING;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY) {
		status = TCP_RUNTIME_LISTENER_DISABLED;
		goto out;
	}
	if (tcp_core_connection_count(&connection_table) >=
	    TCP_RUNTIME_CONNECTION_LIMIT) {
		status = TCP_RUNTIME_LISTENER_RESOURCE_EXHAUSTED;
		goto out;
	}
	for (size_t i = 0; i < 4; i++)
		tuple.local_address[i] = local_address[i];
	tuple.local_port = local_port;	/* zero remote address/port marks a listener */
	if (!tcp_core_connection_create(&connection_table, &tuple, &created)) {
		status = TCP_RUNTIME_LISTENER_CONFLICT;
		goto out;
	}
	if (!tcp_core_step(&connection_table, created, &event, &result) ||
	    result.state != TCP_STATE_LISTEN || result.actions != 0) {
		(void)tcp_core_connection_release(&connection_table, created);
		status = TCP_RUNTIME_LISTENER_CORE_ERROR;
		goto out;
	}
	*handle = created;
	status = TCP_RUNTIME_LISTENER_OK;
out:
	runtime_guard_leave(flags);
	return status;
}

bool
tcp_runtime_listener_close(struct tcp_connection_handle handle)
{
	struct tcp_core_result snapshot, result;
	struct tcp_core_event event = {.type = TCP_EVENT_APPLICATION_CLOSE};
	uint64_t flags = runtime_guard_enter();
	bool closed = false;
	if ((runtime_state != TCP_RUNTIME_READY &&
	    runtime_state != TCP_RUNTIME_STOPPING) ||
	    !tcp_core_connection_get(&connection_table, handle, &snapshot) ||
	    snapshot.state != TCP_STATE_LISTEN ||
#ifdef TCP_RUNTIME_TESTING
	    listener_has_children(handle) ||
#endif
	    !tcp_core_step(&connection_table, handle, &event, &result) ||
	    result.state != TCP_STATE_CLOSED || result.actions != 0 ||
	    !runtime_terminal_cleanup(handle, result.state))
		goto out;
	closed = true;
out:
	runtime_guard_leave(flags);
	return closed;
}

bool
tcp_runtime_handle_release(struct tcp_connection_handle handle)
{
	uint64_t flags = runtime_guard_enter();
	bool released = handle_release_locked(handle);
	runtime_guard_leave(flags);
	return released;
}

#ifdef TCP_RUNTIME_TESTING
static void
segment_to_core_view(const struct tcp_wire_segment *segment,
	struct tcp_segment_view *view)
{
	*view = (struct tcp_segment_view){
		.source_port = segment->source_port,
		.destination_port = segment->destination_port,
		.sequence = segment->sequence,
		.acknowledgment = segment->acknowledgment,
		.flags = segment->flags,
		.window = segment->window,
		.mss_present = segment->parsed_options.mss_present,
		.mss = segment->parsed_options.mss,
		.payload = segment->payload,
		.payload_length = segment->payload_length
	};
}

static enum tcp_runtime_test_input_status
input_result_set(struct tcp_runtime_test_input_result *result,
	enum tcp_runtime_test_input_status status)
{
	result->status = status;
	return status;
}

static void
test_core_result_copy(struct tcp_runtime_test_core_result *destination,
	const struct tcp_core_result *source)
{
	*destination = (struct tcp_runtime_test_core_result){
		.actions = source->actions,
		.state = source->state,
		.snd_una = source->snd_una,
		.snd_nxt = source->snd_nxt,
		.rcv_nxt = source->rcv_nxt,
		.send_window = source->send_window,
		.receive_window = source->receive_window,
		.timer_armed = source->timer_armed,
		.timer_deadline = source->timer_deadline,
		.timer_interval = source->timer_interval,
		.retransmissions = source->retransmissions,
		.accepted_bytes = source->accepted_bytes,
		.has_child = source->has_child
	};
}

static bool
runtime_tuple_equal(const struct tcp_core_tuple *a,
	const struct tcp_core_tuple *b)
{
	return a->local_port == b->local_port &&
	    a->remote_port == b->remote_port &&
	    address_equal(a->local_address, b->local_address) &&
	    address_equal(a->remote_address, b->remote_address);
}

static void
active_result_copy(struct tcp_runtime_test_active_result *result,
	const struct tcp_core_result *core)
{
	result->actions = core->actions;
	result->state = core->state;
	result->snd_una = core->snd_una;
	result->snd_nxt = core->snd_nxt;
	result->rcv_nxt = core->rcv_nxt;
	result->send_window = core->send_window;
	result->receive_window = core->receive_window;
	result->timer_armed = core->timer_armed;
	result->accepted_bytes = core->accepted_bytes;
	result->has_child = core->has_child;
	result->timer_deadline = core->timer_deadline;
	result->timer_interval = core->timer_interval;
	result->retransmissions = core->retransmissions;
}

enum tcp_runtime_test_active_status
tcp_runtime_test_active_open(const struct tcp_core_tuple *tuple,
	uint32_t deterministic_isn, uint64_t now_tick,
	struct tcp_runtime_test_active_result *result)
{
	struct tcp_connection_handle handle, existing;
	struct tcp_core_tuple existing_tuple;
	struct tcp_core_result core_result;
	struct tcp_core_event event = {
		.type = TCP_EVENT_ACTIVE_OPEN,
		.initial_sequence = deterministic_isn,
		.now_tick = now_tick
	};
	enum tcp_runtime_test_active_status status;
	enum tcp_core_lookup_status lookup;
	uint64_t flags;
	if (result == NULL)
		return TCP_RUNTIME_TEST_ACTIVE_INVALID;
	*result = (struct tcp_runtime_test_active_result){
		.status = TCP_RUNTIME_TEST_ACTIVE_INVALID
	};
	if (tuple == NULL || !address_unicast(tuple->local_address) ||
	    !address_unicast(tuple->remote_address) || tuple->local_port == 0 ||
	    tuple->remote_port == 0)
		return result->status;
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_TEST_ACTIVE_STOPPING;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY) {
		status = TCP_RUNTIME_TEST_ACTIVE_DISABLED;
		goto out;
	}
	lookup = tcp_core_connection_lookup(&connection_table, tuple, &existing);
	if (lookup == TCP_CORE_LOOKUP_FOUND) {
		bool duplicate = tcp_core_connection_get_tuple(&connection_table,
		    existing, &existing_tuple) &&
		    existing_tuple.remote_port != 0 &&
		    runtime_tuple_equal(&existing_tuple, tuple);
		(void)tcp_core_connection_release(&connection_table, existing);
		if (duplicate) {
			status = TCP_RUNTIME_TEST_ACTIVE_DUPLICATE;
			goto out;
		}
	} else if (lookup == TCP_CORE_LOOKUP_REFERENCE_LIMIT ||
	    lookup == TCP_CORE_LOOKUP_AMBIGUOUS) {
		status = TCP_RUNTIME_TEST_ACTIVE_RESOURCE_EXHAUSTED;
		goto out;
	} else if (lookup == TCP_CORE_LOOKUP_INVALID) {
		status = TCP_RUNTIME_TEST_ACTIVE_INVALID;
		goto out;
	}
	if (!tcp_core_connection_create(&connection_table, tuple, &handle)) {
		status = tcp_core_connection_count(&connection_table) >=
		    TCP_RUNTIME_CONNECTION_LIMIT ?
		    TCP_RUNTIME_TEST_ACTIVE_RESOURCE_EXHAUSTED :
		    TCP_RUNTIME_TEST_ACTIVE_DUPLICATE;
		goto out;
	}
	if (!tcp_core_step(&connection_table, handle, &event, &core_result) ||
	    core_result.state != TCP_STATE_SYN_SENT ||
	    core_result.actions != TCP_ACTION_SEND_SYN || !core_result.timer_armed) {
		struct tcp_core_event close_event = {
			.type = TCP_EVENT_APPLICATION_CLOSE,
			.now_tick = now_tick
		};
		struct tcp_core_result ignored;
		(void)tcp_core_step(&connection_table, handle, &close_event, &ignored);
		(void)tcp_core_connection_release(&connection_table, handle);
		status = TCP_RUNTIME_TEST_ACTIVE_CORE_ERROR;
		goto out;
	}
	result->handle = handle; /* creation reference transfers to caller */
	result->has_reference = true;
	active_result_copy(result, &core_result);
	status = TCP_RUNTIME_TEST_ACTIVE_OK;
out:
	result->status = status;
	runtime_guard_leave(flags);
	return status;
}

enum tcp_runtime_test_active_status
tcp_runtime_test_active_input(struct tcp_connection_handle handle,
	const uint8_t source_address[4], const uint8_t destination_address[4],
	const uint8_t *segment_bytes, size_t segment_length, uint64_t now_tick,
	struct tcp_runtime_test_active_result *result)
{
	struct tcp_wire_pseudo_header pseudo = {0};
	struct tcp_wire_segment parsed;
	struct tcp_segment_view view;
	struct tcp_core_tuple tuple, incoming = {0};
	struct tcp_core_result snapshot, core_result;
	struct tcp_core_event event = {.type = TCP_EVENT_SEGMENT};
	enum tcp_runtime_test_active_status status;
	uint64_t flags;
	if (result == NULL)
		return TCP_RUNTIME_TEST_ACTIVE_INVALID;
	*result = (struct tcp_runtime_test_active_result){
		.status = TCP_RUNTIME_TEST_ACTIVE_INVALID
	};
	if (source_address == NULL || destination_address == NULL ||
	    segment_bytes == NULL || segment_length > UINT16_MAX ||
	    !address_unicast(source_address) ||
	    !address_unicast(destination_address))
		return result->status;
	for (size_t i = 0; i < 4; i++) {
		pseudo.source[i] = source_address[i];
		pseudo.destination[i] = destination_address[i];
	}
	pseudo.protocol = 6;
	pseudo.segment_length = (uint16_t)segment_length;
	if (tcp_wire_parse(&pseudo, segment_bytes, segment_length, &parsed) !=
	    TCP_WIRE_OK) {
		result->status = TCP_RUNTIME_TEST_ACTIVE_MALFORMED;
		return result->status;
	}
	if (parsed.payload_length != 0) {
		result->status = TCP_RUNTIME_TEST_ACTIVE_UNSUPPORTED;
		return result->status;
	}
	for (size_t i = 0; i < 4; i++) {
		incoming.local_address[i] = destination_address[i];
		incoming.remote_address[i] = source_address[i];
	}
	incoming.local_port = parsed.destination_port;
	incoming.remote_port = parsed.source_port;
	segment_to_core_view(&parsed, &view);
	event.segment = &view;
	event.now_tick = now_tick;
	for (size_t i = 0; i < 4; i++) {
		event.source_address[i] = source_address[i];
		event.destination_address[i] = destination_address[i];
	}
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_TEST_ACTIVE_STOPPING;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY) {
		status = TCP_RUNTIME_TEST_ACTIVE_DISABLED;
		goto out;
	}
	if (!tcp_core_connection_get_tuple(&connection_table, handle, &tuple) ||
	    !tcp_core_connection_get(&connection_table, handle, &snapshot)) {
		status = TCP_RUNTIME_TEST_ACTIVE_STALE_HANDLE;
		goto out;
	}
	if (child_link_find(handle) != NULL || tuple.remote_port == 0 ||
	    snapshot.state == TCP_STATE_CLOSED ||
	    snapshot.state == TCP_STATE_LISTEN) {
		status = TCP_RUNTIME_TEST_ACTIVE_NOT_ACTIVE;
		goto out;
	}
	if (!runtime_tuple_equal(&tuple, &incoming)) {
		status = TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH;
		goto out;
	}
	if (!tcp_core_step(&connection_table, handle, &event, &core_result)) {
		status = TCP_RUNTIME_TEST_ACTIVE_CORE_ERROR;
		goto out;
	}
	active_result_copy(result, &core_result);
	if (!runtime_terminal_cleanup(handle, core_result.state)) {
		status = TCP_RUNTIME_TEST_ACTIVE_CORE_ERROR;
		goto out;
	}
	status = TCP_RUNTIME_TEST_ACTIVE_OK;
out:
	result->status = status;
	runtime_guard_leave(flags);
	return status;
}

enum tcp_runtime_test_input_status
tcp_runtime_test_process_segment(const uint8_t source_address[4],
	const uint8_t destination_address[4],
	const struct tcp_wire_segment *segment, uint32_t deterministic_isn,
	uint64_t now_tick, struct tcp_runtime_test_input_result *result)
{
	struct tcp_core_tuple tuple = {0};
	struct tcp_core_tuple matched_tuple;
	struct tcp_connection_handle matched;
	struct tcp_connection_handle listener = {0};
	struct tcp_segment_view view;
	struct tcp_core_event event = {0};
	struct tcp_core_result before, core_result;
	enum tcp_core_lookup_status lookup;
	enum tcp_runtime_test_input_status status;
	struct runtime_child_link *link = NULL;
	uint64_t flags;
	bool is_syn, is_ack, is_reset, is_fin;
	if (result == NULL)
		return TCP_RUNTIME_TEST_INPUT_INVALID;
	*result = (struct tcp_runtime_test_input_result){
		.status = TCP_RUNTIME_TEST_INPUT_INVALID
	};
	if (source_address == NULL || destination_address == NULL ||
	    !address_unicast(source_address) ||
	    !address_unicast(destination_address) || !parsed_segment_valid(segment))
		return result->status;
	is_syn = segment->flags == TCP_FLAG_SYN && segment->payload_length == 0;
	is_ack = segment->flags == TCP_FLAG_ACK && segment->payload_length == 0;
	is_reset = (segment->flags == TCP_FLAG_RST ||
	    segment->flags == (TCP_FLAG_RST | TCP_FLAG_ACK)) &&
	    segment->payload_length == 0;
	is_fin = (segment->flags == TCP_FLAG_FIN ||
	    segment->flags == (TCP_FLAG_FIN | TCP_FLAG_ACK)) &&
	    segment->payload_length == 0;
	if (!is_syn && !is_ack && !is_reset && !is_fin)
		return input_result_set(result,
		    TCP_RUNTIME_TEST_INPUT_UNSUPPORTED);
	for (size_t i = 0; i < 4; i++) {
		tuple.local_address[i] = destination_address[i];
		tuple.remote_address[i] = source_address[i];
	}
	tuple.local_port = segment->destination_port;
	tuple.remote_port = segment->source_port;
	segment_to_core_view(segment, &view);
	event = (struct tcp_core_event){
		.type = TCP_EVENT_SEGMENT,
		.initial_sequence = deterministic_isn,
		.now_tick = now_tick,
		.segment = &view
	};
	for (size_t i = 0; i < 4; i++) {
		event.source_address[i] = source_address[i];
		event.destination_address[i] = destination_address[i];
	}
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_TEST_INPUT_STOPPING;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY) {
		status = TCP_RUNTIME_TEST_INPUT_INVALID;
		goto out;
	}
	lookup = tcp_core_connection_lookup(&connection_table, &tuple, &matched);
	if (lookup == TCP_CORE_LOOKUP_AMBIGUOUS) {
		status = TCP_RUNTIME_TEST_INPUT_AMBIGUOUS;
		goto out;
	}
	if (lookup == TCP_CORE_LOOKUP_REFERENCE_LIMIT) {
		status = TCP_RUNTIME_TEST_INPUT_RESOURCE_EXHAUSTED;
		goto out;
	}
	if (lookup == TCP_CORE_LOOKUP_INVALID) {
		status = TCP_RUNTIME_TEST_INPUT_INVALID;
		goto out;
	}
	if (lookup == TCP_CORE_LOOKUP_NOT_FOUND) {
		status = TCP_RUNTIME_TEST_INPUT_NO_MATCH;
		goto out;
	}
	if (lookup == TCP_CORE_LOOKUP_FOUND) {
		if (!tcp_core_connection_get_tuple(&connection_table, matched,
		    &matched_tuple) ||
		    !tcp_core_connection_get(&connection_table, matched, &before)) {
			(void)tcp_core_connection_release(&connection_table, matched);
			status = TCP_RUNTIME_TEST_INPUT_INVALID;
			goto out;
		}
		if (matched_tuple.remote_port == 0) {
			if (!is_syn || before.state != TCP_STATE_LISTEN) {
				(void)tcp_core_connection_release(&connection_table, matched);
				status = is_syn ? TCP_RUNTIME_TEST_INPUT_CORE_REJECTED :
				    TCP_RUNTIME_TEST_INPUT_NO_MATCH;
				goto out;
			}
			listener = matched;
			goto process_listener;
		}
		if (before.state == TCP_STATE_CLOSED ||
		    before.state == TCP_STATE_LISTEN ||
		    (is_syn && (before.state != TCP_STATE_SYN_RECEIVED ||
		    segment->sequence + 1U != before.rcv_nxt))) {
			(void)tcp_core_connection_release(&connection_table, matched);
			status = TCP_RUNTIME_TEST_INPUT_UNSUPPORTED;
			goto out;
		}
		link = child_link_find(matched);
		if (link == NULL) {
			(void)tcp_core_connection_release(&connection_table, matched);
			status = TCP_RUNTIME_TEST_INPUT_INVALID;
			goto out;
		}
		if (!tcp_core_step(&connection_table, matched, &event, &core_result)) {
			(void)tcp_core_connection_release(&connection_table, matched);
			status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
			goto out;
		}
		/* Preserve the exact Core outcome even when Runtime classifies the
		 * event as unsupported or non-establishing. Actions such as SEND_RST
		 * must not disappear behind a Runtime status code. */
		test_core_result_copy(&result->outcome, &core_result);
		if (runtime_state_terminal(core_result.state)) {
			if (!runtime_terminal_cleanup(matched, core_result.state)) {
				(void)tcp_core_connection_release(&connection_table, matched);
				status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
				goto out;
			}
			(void)tcp_core_connection_release(&connection_table, matched);
			status = is_reset ? TCP_RUNTIME_TEST_INPUT_CHILD_CLOSED :
			    TCP_RUNTIME_TEST_INPUT_PROCESSED;
			goto out;
		}
		if (is_syn && core_result.state == TCP_STATE_SYN_RECEIVED &&
		    (core_result.actions & TCP_ACTION_RETRANSMIT) != 0) {
			result->handle = matched;
			result->has_reference = true;
			status = TCP_RUNTIME_TEST_INPUT_DUPLICATE_SYN;
		} else if (core_result.state == TCP_STATE_ESTABLISHED) {
			result->handle = matched;
			result->has_reference = true;
			status = TCP_RUNTIME_TEST_INPUT_ESTABLISHED;
		} else if (is_fin) {
			(void)tcp_core_connection_release(&connection_table, matched);
			status = TCP_RUNTIME_TEST_INPUT_PROCESSED;
		} else {
			(void)tcp_core_connection_release(&connection_table, matched);
			status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
		}
		goto out;
	}
	if (!is_syn) {
		status = TCP_RUNTIME_TEST_INPUT_NO_MATCH;
		goto out;
	}
process_listener:
	if (!tcp_core_connection_get(&connection_table, listener, &before) ||
	    before.state != TCP_STATE_LISTEN) {
		(void)tcp_core_connection_release(&connection_table, listener);
		status = TCP_RUNTIME_TEST_INPUT_NO_MATCH;
		goto out;
	}
	if (listener_pending_children(listener) >= TCP_CORE_LISTEN_BACKLOG ||
	    tcp_core_connection_count(&connection_table) >=
	    TCP_RUNTIME_CONNECTION_LIMIT) {
		(void)tcp_core_connection_release(&connection_table, listener);
		status = TCP_RUNTIME_TEST_INPUT_RESOURCE_EXHAUSTED;
		goto out;
	}
	if (!tcp_core_step(&connection_table, listener, &event, &core_result)) {
		(void)tcp_core_connection_release(&connection_table, listener);
		status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
		goto out;
	}
	test_core_result_copy(&result->outcome, &core_result);
	(void)tcp_core_connection_release(&connection_table, listener);
	if (!core_result.has_child ||
	    core_result.state != TCP_STATE_LISTEN ||
	    core_result.child.slot >= TCP_RUNTIME_CONNECTION_LIMIT ||
	    child_links[core_result.child.slot].occupied ||
	    !tcp_core_connection_get(&connection_table, core_result.child,
	    &before) || before.state != TCP_STATE_SYN_RECEIVED ||
	    !tcp_core_connection_get_tuple(&connection_table, core_result.child,
	    &tuple) || tuple.local_port != segment->destination_port ||
	    tuple.remote_port != segment->source_port ||
	    !address_equal(tuple.local_address, destination_address) ||
	    !address_equal(tuple.remote_address, source_address)) {
		/* Core-created child's first reference is still ours; close table
		 * ownership and release it if validation unexpectedly fails. */
		if (core_result.has_child) {
			struct tcp_core_event close_event = {
				.type = TCP_EVENT_APPLICATION_CLOSE
			};
			struct tcp_core_result ignored;
			(void)tcp_core_step(&connection_table, core_result.child,
			    &close_event, &ignored);
			(void)tcp_core_connection_release(&connection_table,
			    core_result.child);
		}
		status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
		goto out;
	}
	/* A relationship owns one child reference and one listener reference. The
	 * initial child reference is transferred into the link; retain a separate
	 * caller handle and pin the parent generation until terminal unlink. */
	if (!tcp_core_connection_retain(&connection_table, listener)) {
		struct tcp_core_event close_event = {
			.type = TCP_EVENT_APPLICATION_CLOSE
		};
		struct tcp_core_result ignored;
		(void)tcp_core_step(&connection_table, core_result.child,
		    &close_event, &ignored);
		(void)tcp_core_connection_release(&connection_table,
		    core_result.child);
		status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
		goto out;
	}
	if (!tcp_core_connection_retain(&connection_table, core_result.child)) {
		struct tcp_core_event close_event = {
			.type = TCP_EVENT_APPLICATION_CLOSE
		};
		struct tcp_core_result ignored;
		(void)tcp_core_step(&connection_table, core_result.child,
		    &close_event, &ignored);
		(void)tcp_core_connection_release(&connection_table,
		    core_result.child);
		(void)tcp_core_connection_release(&connection_table, listener);
		status = TCP_RUNTIME_TEST_INPUT_CORE_REJECTED;
		goto out;
	}
	/* The link owns the Core-created child reference and the retained parent
	 * reference. The additional child reference returned below belongs to the
	 * caller. */
	child_links[core_result.child.slot] = (struct runtime_child_link){
		.occupied = true,
		.child = core_result.child,
		.listener = listener
	};
	result->handle = core_result.child;
	result->has_reference = true;
	status = TCP_RUNTIME_TEST_INPUT_CHILD_CREATED;
out:
	result->status = status;
	runtime_guard_leave(flags);
	return status;
}
#endif

static bool
parsed_segment_valid(const struct tcp_wire_segment *segment)
{
	size_t header_length;
	if (segment == NULL || segment->source_port == 0 ||
	    segment->destination_port == 0 || segment->data_offset < 5 ||
	    segment->data_offset > TCP_WIRE_HEADER_MAX / 4U ||
	    segment->options_length > TCP_WIRE_OPTIONS_MAX ||
	    (segment->options_length != 0 && segment->options == NULL) ||
	    (segment->payload_length != 0 && segment->payload == NULL))
		return false;
	header_length = (size_t)segment->data_offset * 4U;
	return header_length == TCP_WIRE_HEADER_MIN + segment->options_length &&
	    segment->payload_length <= TCP_WIRE_SEGMENT_MAX - header_length;
}

enum tcp_runtime_route_status
tcp_runtime_route_segment(const uint8_t source_address[4],
	const uint8_t destination_address[4],
	const struct tcp_wire_segment *segment,
	struct tcp_runtime_route_result *result)
{
#ifdef OPEN_SILLICON_ENABLE_TCP_ROUTING
	struct tcp_core_tuple tuple = {0};
	struct tcp_core_tuple matched_tuple;
	struct tcp_connection_handle handle;
	enum tcp_core_lookup_status lookup;
#endif
	enum tcp_runtime_route_status status;
	uint64_t flags;
	if (result == NULL)
		return TCP_RUNTIME_ROUTE_INVALID;
	*result = (struct tcp_runtime_route_result){
		.status = TCP_RUNTIME_ROUTE_INVALID
	};
	if (source_address == NULL || destination_address == NULL ||
	    !address_unicast(source_address) ||
	    !address_unicast(destination_address) ||
	    !parsed_segment_valid(segment))
		return result->status;
	result->payload_present = segment->payload_length != 0;
	result->payload_supported = false;
	result->event_submitted = false;
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_ROUTE_STOPPING;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY) {
		status = TCP_RUNTIME_ROUTE_DISABLED;
		goto out;
	}
#ifndef OPEN_SILLICON_ENABLE_TCP_ROUTING
	status = TCP_RUNTIME_ROUTE_DISABLED;
	goto out;
#else
	for (size_t i = 0; i < 4; i++) {
		tuple.local_address[i] = destination_address[i];
		tuple.remote_address[i] = source_address[i];
	}
	tuple.local_port = segment->destination_port;
	tuple.remote_port = segment->source_port;
	lookup = tcp_core_connection_lookup(&connection_table, &tuple, &handle);
	switch (lookup) {
	case TCP_CORE_LOOKUP_FOUND:
		if (!tcp_core_connection_get_tuple(&connection_table, handle,
		    &matched_tuple)) {
			(void)tcp_core_connection_release(&connection_table, handle);
			status = TCP_RUNTIME_ROUTE_INVALID;
			goto out;
		}
		result->handle = handle;
		result->has_reference = true;
		status = matched_tuple.remote_port == 0 ?
		    TCP_RUNTIME_ROUTE_LISTENER : TCP_RUNTIME_ROUTE_CONNECTION;
		break;
	case TCP_CORE_LOOKUP_NOT_FOUND:
		status = TCP_RUNTIME_ROUTE_NO_MATCH;
		break;
	case TCP_CORE_LOOKUP_AMBIGUOUS:
		status = TCP_RUNTIME_ROUTE_AMBIGUOUS;
		break;
	case TCP_CORE_LOOKUP_REFERENCE_LIMIT:
		status = TCP_RUNTIME_ROUTE_RESOURCE_EXHAUSTED;
		break;
	case TCP_CORE_LOOKUP_INVALID:
	default:
		status = TCP_RUNTIME_ROUTE_INVALID;
		break;
	}
#endif
out:
	result->status = status;
	runtime_guard_leave(flags);
	return status;
}

#ifdef TCP_RUNTIME_TESTING
bool
tcp_runtime_test_reserve(const struct tcp_core_tuple *tuple,
	struct tcp_connection_handle *handle)
{
	uint64_t flags = runtime_guard_enter();
	bool success = runtime_state == TCP_RUNTIME_READY &&
	    tcp_core_connection_create(&connection_table, tuple, handle);
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_retain(struct tcp_connection_handle handle)
{
	uint64_t flags = runtime_guard_enter();
	bool success = (runtime_state == TCP_RUNTIME_READY ||
	    runtime_state == TCP_RUNTIME_STOPPING) &&
	    tcp_core_connection_retain(&connection_table, handle);
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_activate(struct tcp_connection_handle handle,
	uint32_t deterministic_seed,
	struct tcp_runtime_test_core_result *result)
{
	struct tcp_core_result core_result;
	struct tcp_core_event event = {
		.type = TCP_EVENT_ACTIVE_OPEN,
		.initial_sequence = deterministic_seed
	};
	if (result == NULL)
		return false;
	*result = (struct tcp_runtime_test_core_result){0};
	uint64_t flags = runtime_guard_enter();
	bool success = runtime_state == TCP_RUNTIME_READY &&
	    tcp_core_step(&connection_table, handle, &event, &core_result);
	if (success)
		test_core_result_copy(result, &core_result);
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_close(struct tcp_connection_handle handle,
	uint64_t now_tick, struct tcp_runtime_test_core_result *result)
{
	struct tcp_core_result core_result;
	struct tcp_core_event event = {
		.type = TCP_EVENT_APPLICATION_CLOSE,
		.now_tick = now_tick
	};
	struct tcp_core_tuple tuple;
	if (result == NULL)
		return false;
	*result = (struct tcp_runtime_test_core_result){0};
	uint64_t flags = runtime_guard_enter();
	bool success = false;
	if ((runtime_state != TCP_RUNTIME_READY &&
	    runtime_state != TCP_RUNTIME_STOPPING) ||
	    !tcp_core_connection_get_tuple(&connection_table, handle, &tuple))
		goto out;
	/* Listeners close through their dedicated API so child-link policy cannot
	 * be bypassed by this generic test-only connection close. */
	if (tuple.remote_port == 0 &&
	    (child_link_find(handle) != NULL || listener_has_children(handle)))
		goto out;
	if (!tcp_core_step(&connection_table, handle, &event, &core_result))
		goto out;
	test_core_result_copy(result, &core_result);
	if (!runtime_terminal_cleanup(handle, core_result.state))
		goto out;
	success = true;
out:
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_release(struct tcp_connection_handle handle)
{
	return tcp_runtime_handle_release(handle);
}

bool
tcp_runtime_test_snapshot(struct tcp_connection_handle handle,
	struct tcp_runtime_test_core_result *result)
{
	struct tcp_core_result core_result;
	if (result == NULL)
		return false;
	*result = (struct tcp_runtime_test_core_result){0};
	uint64_t flags = runtime_guard_enter();
	bool success = (runtime_state == TCP_RUNTIME_READY ||
	    runtime_state == TCP_RUNTIME_STOPPING) &&
	    tcp_core_connection_get(&connection_table, handle, &core_result);
	if (success)
		test_core_result_copy(result, &core_result);
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_inject_listener_alias(struct tcp_connection_handle listener,
	struct tcp_connection_handle *alias)
{
	struct tcp_core_connection source;
	struct tcp_core_connection *free_slot = NULL;
	uint8_t source_slot;
	uint32_t generation;
	uint64_t flags;
	if (alias == NULL)
		return false;
	flags = runtime_guard_enter();
	if (runtime_state != TCP_RUNTIME_READY ||
	    listener.slot >= TCP_CORE_CONNECTION_LIMIT ||
	    connection_table.slots[listener.slot].generation != listener.generation ||
	    !connection_table.slots[listener.slot].allocated ||
	    connection_table.slots[listener.slot].state != TCP_STATE_LISTEN)
		goto fail;
	source_slot = listener.slot;
	source = connection_table.slots[source_slot];
	for (size_t i = 0; i < TCP_CORE_CONNECTION_LIMIT; i++)
		if (!connection_table.slots[i].allocated) {
			free_slot = &connection_table.slots[i];
			break;
		}
	if (free_slot == NULL)
		goto fail;
	generation = connection_table.next_generation++;
	if (generation == 0) {
		generation = connection_table.next_generation++;
		if (generation == 0)
			generation = 1;
	}
	if (connection_table.next_generation == 0)
		connection_table.next_generation = 1;
	*free_slot = source;
	free_slot->generation = generation;
	free_slot->external_references = 1;
	*alias = (struct tcp_connection_handle){
		.slot = (uint8_t)(free_slot - connection_table.slots),
		.generation = generation
	};
	runtime_guard_leave(flags);
	return true;
fail:
	runtime_guard_leave(flags);
	return false;
}

bool
tcp_runtime_test_set_references(struct tcp_connection_handle handle,
	uint16_t references)
{
	uint64_t flags = runtime_guard_enter();
	bool success = false;
	if ((runtime_state == TCP_RUNTIME_READY ||
	    runtime_state == TCP_RUNTIME_STOPPING) &&
	    handle.slot < TCP_CORE_CONNECTION_LIMIT &&
	    connection_table.slots[handle.slot].allocated &&
	    connection_table.slots[handle.slot].generation == handle.generation) {
		connection_table.slots[handle.slot].external_references = references;
		success = true;
	}
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_set_timer_service_active(bool active)
{
	uint64_t flags = runtime_guard_enter();
	bool success = runtime_state == TCP_RUNTIME_READY;
	if (success)
		timer_service_active = active;
	runtime_guard_leave(flags);
	return success;
}

bool
tcp_runtime_test_reference_count(struct tcp_connection_handle handle,
	uint16_t *references)
{
	uint64_t flags = runtime_guard_enter();
	bool success = false;
	if (references != NULL &&
	    (runtime_state == TCP_RUNTIME_READY ||
	    runtime_state == TCP_RUNTIME_STOPPING) &&
	    handle.slot < TCP_CORE_CONNECTION_LIMIT &&
	    connection_table.slots[handle.slot].allocated &&
	    connection_table.slots[handle.slot].generation == handle.generation) {
		*references = connection_table.slots[handle.slot].external_references;
		success = true;
	}
	runtime_guard_leave(flags);
	return success;
}

static void
test_close_result_copy(struct tcp_runtime_test_close_result *destination,
	const struct tcp_core_result *source)
{
	*destination = (struct tcp_runtime_test_close_result){
		.state = source->state,
		.actions = source->actions,
		.snd_una = source->snd_una,
		.snd_nxt = source->snd_nxt,
		.rcv_nxt = source->rcv_nxt,
		.send_window = source->send_window,
		.receive_window = source->receive_window,
		.accepted_bytes = source->accepted_bytes,
		.has_child = source->has_child,
		.timer_deadline = source->timer_deadline,
		.timer_interval = source->timer_interval,
		.retransmissions = source->retransmissions,
		.timer_armed = source->timer_armed
	};
}

enum tcp_runtime_test_close_status
tcp_runtime_test_local_close(struct tcp_connection_handle handle,
	uint64_t now_tick, struct tcp_runtime_test_close_result *result)
{
	struct tcp_core_tuple tuple;
	struct tcp_core_result before, after;
	struct tcp_core_event event = {
		.type = TCP_EVENT_APPLICATION_CLOSE,
		.now_tick = now_tick
	};
	enum tcp_runtime_test_close_status status;
	uint64_t flags;

	if (result == NULL)
		return TCP_RUNTIME_TEST_CLOSE_INVALID;
	*result = (struct tcp_runtime_test_close_result){
		.status = TCP_RUNTIME_TEST_CLOSE_INVALID
	};
	flags = runtime_guard_enter();
	if (runtime_state == TCP_RUNTIME_STOPPED ||
	    runtime_state == TCP_RUNTIME_UNINITIALIZED) {
		status = TCP_RUNTIME_TEST_CLOSE_DISABLED;
		goto out;
	}
	if (runtime_state != TCP_RUNTIME_READY &&
	    runtime_state != TCP_RUNTIME_STOPPING) {
		status = TCP_RUNTIME_TEST_CLOSE_DISABLED;
		goto out;
	}
	if (timer_service_active) {
		status = TCP_RUNTIME_TEST_CLOSE_BUSY;
		goto out;
	}
	if (!tcp_core_connection_get(&connection_table, handle, &before) ||
	    !tcp_core_connection_get_tuple(&connection_table, handle, &tuple)) {
		status = TCP_RUNTIME_TEST_CLOSE_STALE_HANDLE;
		goto out;
	}
	/* Listener lifecycle has a separate API that enforces child-link rules. */
	if (tuple.remote_port == 0 || before.state == TCP_STATE_CLOSED ||
	    before.state == TCP_STATE_LISTEN ||
	    before.state == TCP_STATE_FIN_WAIT_1 ||
	    before.state == TCP_STATE_FIN_WAIT_2 ||
	    before.state == TCP_STATE_CLOSING ||
	    before.state == TCP_STATE_LAST_ACK ||
	    before.state == TCP_STATE_TIME_WAIT) {
		test_close_result_copy(result, &before);
		status = TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED;
		goto out;
	}
	if (before.state != TCP_STATE_ESTABLISHED &&
	    before.state != TCP_STATE_CLOSE_WAIT &&
	    before.state != TCP_STATE_SYN_SENT &&
	    before.state != TCP_STATE_SYN_RECEIVED) {
		test_close_result_copy(result, &before);
		status = TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED;
		goto out;
	}
	if (!tcp_core_step(&connection_table, handle, &event, &after)) {
		test_close_result_copy(result, &before);
		status = TCP_RUNTIME_TEST_CLOSE_CORE_ERROR;
		goto out;
	}
	test_close_result_copy(result, &after);
	if (!runtime_terminal_cleanup(handle, after.state)) {
		status = TCP_RUNTIME_TEST_CLOSE_CORE_ERROR;
		goto out;
	}
	if (before.state == TCP_STATE_SYN_SENT ||
	    before.state == TCP_STATE_SYN_RECEIVED) {
		status = after.state == TCP_STATE_CLOSED &&
		    (after.actions & TCP_ACTION_SEND_RST) != 0 ?
		    TCP_RUNTIME_TEST_CLOSE_HANDSHAKE_ABORTED :
		    TCP_RUNTIME_TEST_CLOSE_CORE_ERROR;
	} else {
		status = ((before.state == TCP_STATE_ESTABLISHED &&
		    after.state == TCP_STATE_FIN_WAIT_1) ||
		    (before.state == TCP_STATE_CLOSE_WAIT &&
		    after.state == TCP_STATE_LAST_ACK)) &&
		    (after.actions & (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK)) ==
		    (TCP_ACTION_SEND_FIN | TCP_ACTION_SEND_ACK) ?
		    TCP_RUNTIME_TEST_CLOSE_FIN_STARTED :
		    TCP_RUNTIME_TEST_CLOSE_CORE_ERROR;
	}
out:
	result->status = status;
	runtime_guard_leave(flags);
	return status;
}
#endif
