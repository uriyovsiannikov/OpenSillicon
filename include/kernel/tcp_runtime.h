#ifndef KERNEL_TCP_RUNTIME_H
#define KERNEL_TCP_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "kernel/tcp_core.h"
#include "kernel/tcp_wire.h"

#define TCP_RUNTIME_CONNECTION_LIMIT TCP_CORE_CONNECTION_LIMIT
#define TCP_RUNTIME_TIMER_EVENT_LIMIT TCP_CORE_SERVICE_EVENT_LIMIT

enum tcp_runtime_listener_status {
	TCP_RUNTIME_LISTENER_OK = 0,
	TCP_RUNTIME_LISTENER_INVALID,
	TCP_RUNTIME_LISTENER_DISABLED,
	TCP_RUNTIME_LISTENER_STOPPING,
	TCP_RUNTIME_LISTENER_CONFLICT,
	TCP_RUNTIME_LISTENER_RESOURCE_EXHAUSTED,
	TCP_RUNTIME_LISTENER_CORE_ERROR
};

enum tcp_runtime_route_status {
	TCP_RUNTIME_ROUTE_CONNECTION = 0,
	TCP_RUNTIME_ROUTE_LISTENER,
	TCP_RUNTIME_ROUTE_NO_MATCH,
	TCP_RUNTIME_ROUTE_AMBIGUOUS,
	TCP_RUNTIME_ROUTE_DISABLED,
	TCP_RUNTIME_ROUTE_STOPPING,
	TCP_RUNTIME_ROUTE_INVALID,
	TCP_RUNTIME_ROUTE_RESOURCE_EXHAUSTED
};

enum tcp_runtime_timer_status {
	TCP_RUNTIME_TIMER_OK = 0,
	TCP_RUNTIME_TIMER_DISABLED,
	TCP_RUNTIME_TIMER_STOPPING,
	TCP_RUNTIME_TIMER_BUSY,
	TCP_RUNTIME_TIMER_INVALID,
	TCP_RUNTIME_TIMER_CORE_ERROR,
	TCP_RUNTIME_TIMER_STALE_HANDLE
};

/* One completed Core timer event, projected to scalar fields. `state`, actions,
 * sequence numbers, windows, and timer values are the Core result at the end
 * of that event. `has_child` reports Core metadata only; no Core child handle
 * is exposed. No pointer or handle remains valid after this item is returned. */
struct tcp_runtime_timer_item {
	uint32_t actions;
	enum tcp_connection_state state;
	uint32_t snd_una;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint16_t send_window;
	uint16_t receive_window;
	bool timer_armed;
	size_t accepted_bytes;
	bool has_child;
	uint64_t timer_deadline;
	uint64_t timer_interval;
	uint8_t retransmissions;
};

struct tcp_runtime_timer_result {
	enum tcp_runtime_timer_status status;
	size_t count;
	bool more_due;
	struct tcp_runtime_timer_item items[TCP_RUNTIME_TIMER_EVENT_LIMIT];
};

/* A route match owns one retained generation-tagged handle. All flags are
 * scalar metadata; no input packet or parsed pointer is retained. Payload is
 * reported but unsupported, and no core event is submitted in this phase. */
struct tcp_runtime_route_result {
	enum tcp_runtime_route_status status;
	struct tcp_connection_handle handle;
	bool has_reference;
	bool payload_present;
	bool payload_supported;
	bool event_submitted;
};

/* Owns one fixed TCP_CORE_CONNECTION_LIMIT Core table. Runtime handles use
 * the slot/generation token shape declared by tcp_core.h, but are Runtime
 * references: callers must use tcp_runtime_handle_release(), never Core table
 * operations. The production implementation serializes with local interrupts
 * masked on this single CPU. This is not SMP synchronization; host-test calls
 * must also be externally serialized. */
/* Succeeds only once, from the uninitialized state. No allocation failure is
 * possible; duplicate init and any init after shutdown begins fail unchanged. */
bool tcp_runtime_init(void);
/* Shutdown is non-forcing. From READY it enters sticky STOPPING, rejects new
 * work, and returns false while Core entries/references or timer service
 * remain. Close/release operations may drain state during STOPPING. It returns
 * true and enters STOPPED only when the table is empty and service is idle.
 * STOPPED cannot be reinitialized. Calling before init or after STOPPED fails. */
bool tcp_runtime_shutdown(void);
/* True only in READY; false before init, during STOPPING, and after STOPPED. */
bool tcp_runtime_ready(void);

/* now_tick uses the 100 Hz Core tick domain. Accepts budgets 0..4. A non-null
 * result is zero-initialized even when budget is invalid. On OK, count entries
 * contain Core event outcomes and `more_due` says another bounded call can
 * make progress. Core service references are consumed/released before return;
 * no Runtime/Core handle escapes. Disabled before init/after STOPPED, STOPPING
 * while stopping, BUSY on reentry, INVALID for null result/oversized budget. */
enum tcp_runtime_timer_status tcp_runtime_service_timers(uint64_t now_tick,
	size_t budget, struct tcp_runtime_timer_result *result);

/* Opens a listener on a unicast local address or 0.0.0.0 wildcard and a
 * nonzero port. On success, writes one caller-owned Runtime handle; caller
 * must close the listener and release that handle separately. On failure,
 * `handle` is untouched and no entry/reference remains. Allowed only in READY. */
enum tcp_runtime_listener_status tcp_runtime_listener_open(
	const uint8_t local_address[4], uint16_t local_port,
	struct tcp_connection_handle *handle);
/* Closes Core LISTEN table ownership if no passive child relationship remains.
 * Does not consume caller handles; caller must release each independently.
 * May drain during STOPPING. Stale, non-listener, or linked listeners fail. */
bool tcp_runtime_listener_close(struct tcp_connection_handle handle);
/* Releases exactly one caller-owned Runtime reference. A valid token can be
 * released after Core reaches CLOSED and during STOPPING. Stale generations,
 * before-init, and after-STOPPED calls return false without changing state. */
bool tcp_runtime_handle_release(struct tcp_connection_handle handle);

/* Accepts a parsed wire view and IPv4 tuple (it rechecks scalar/pointer bounds,
 * but caller must have validated the packet checksum). The default production
 * build returns DISABLED without lookup. If the explicit routing compile gate
 * is enabled, a match returns one retained Runtime handle; caller must release
 * it. Even then this API only identifies a tuple: it submits no Core event,
 * accepts no payload, and queues/transmits nothing. */
enum tcp_runtime_route_status tcp_runtime_route_segment(
	const uint8_t source_address[4], const uint8_t destination_address[4],
	const struct tcp_wire_segment *segment,
	struct tcp_runtime_route_result *result);

#ifdef TCP_RUNTIME_TESTING
#define TCP_RUNTIME_TEST_INSTANCE_LIMIT 2U

/* Every declaration below exists only in TCP_RUNTIME_TESTING host builds.
 * Deterministic ISNs and injected packet events are test inputs, not production
 * entropy. Results are initialized before validation; their outcome fields
 * project Core scalars and omit Core handles/pointers. Actions are descriptive
 * only and are never serialized or transmitted. */
enum tcp_runtime_test_input_status {
	TCP_RUNTIME_TEST_INPUT_CHILD_CREATED,
	TCP_RUNTIME_TEST_INPUT_DUPLICATE_SYN,
	TCP_RUNTIME_TEST_INPUT_ESTABLISHED,
	TCP_RUNTIME_TEST_INPUT_CHILD_CLOSED,
	TCP_RUNTIME_TEST_INPUT_NO_MATCH,
	TCP_RUNTIME_TEST_INPUT_AMBIGUOUS,
	TCP_RUNTIME_TEST_INPUT_INVALID,
	TCP_RUNTIME_TEST_INPUT_RESOURCE_EXHAUSTED,
	TCP_RUNTIME_TEST_INPUT_CORE_REJECTED,
	TCP_RUNTIME_TEST_INPUT_UNSUPPORTED,
	TCP_RUNTIME_TEST_INPUT_STOPPING,
	TCP_RUNTIME_TEST_INPUT_PROCESSED
};

enum tcp_runtime_test_active_status {
	TCP_RUNTIME_TEST_ACTIVE_OK = 0,
	TCP_RUNTIME_TEST_ACTIVE_INVALID,
	TCP_RUNTIME_TEST_ACTIVE_DISABLED,
	TCP_RUNTIME_TEST_ACTIVE_STOPPING,
	TCP_RUNTIME_TEST_ACTIVE_DUPLICATE,
	TCP_RUNTIME_TEST_ACTIVE_RESOURCE_EXHAUSTED,
	TCP_RUNTIME_TEST_ACTIVE_MALFORMED,
	TCP_RUNTIME_TEST_ACTIVE_STALE_HANDLE,
	TCP_RUNTIME_TEST_ACTIVE_TUPLE_MISMATCH,
	TCP_RUNTIME_TEST_ACTIVE_NOT_ACTIVE,
	TCP_RUNTIME_TEST_ACTIVE_UNSUPPORTED,
	TCP_RUNTIME_TEST_ACTIVE_CORE_ERROR
};

enum tcp_runtime_test_close_status {
	TCP_RUNTIME_TEST_CLOSE_FIN_STARTED = 0,
	TCP_RUNTIME_TEST_CLOSE_HANDSHAKE_ABORTED,
	TCP_RUNTIME_TEST_CLOSE_UNSUPPORTED,
	TCP_RUNTIME_TEST_CLOSE_DISABLED,
	TCP_RUNTIME_TEST_CLOSE_BUSY,
	TCP_RUNTIME_TEST_CLOSE_STALE_HANDLE,
	TCP_RUNTIME_TEST_CLOSE_INVALID,
	TCP_RUNTIME_TEST_CLOSE_CORE_ERROR
};

enum tcp_runtime_test_io_status {
	TCP_RUNTIME_TEST_IO_OK = 0,
	TCP_RUNTIME_TEST_IO_WOULD_BLOCK,
	TCP_RUNTIME_TEST_IO_EOF,
	TCP_RUNTIME_TEST_IO_RESET,
	TCP_RUNTIME_TEST_IO_INVALID,
	TCP_RUNTIME_TEST_IO_DISABLED,
	TCP_RUNTIME_TEST_IO_STOPPING,
	TCP_RUNTIME_TEST_IO_STALE_HANDLE,
	TCP_RUNTIME_TEST_IO_NOT_CONNECTED,
	TCP_RUNTIME_TEST_IO_FULL,
	TCP_RUNTIME_TEST_IO_TOO_LARGE,
	TCP_RUNTIME_TEST_IO_CORE_ERROR
};

struct tcp_runtime_test_io_result {
	enum tcp_runtime_test_io_status status;
	size_t bytes;
	enum tcp_connection_state state;
};

enum tcp_runtime_test_tx_status {
	TCP_RUNTIME_TEST_TX_OK = 0,
	TCP_RUNTIME_TEST_TX_EMPTY,
	TCP_RUNTIME_TEST_TX_INVALID,
	TCP_RUNTIME_TEST_TX_DISABLED,
	TCP_RUNTIME_TEST_TX_STOPPING,
	TCP_RUNTIME_TEST_TX_STALE_HANDLE,
	TCP_RUNTIME_TEST_TX_OUTPUT_TOO_SMALL,
	TCP_RUNTIME_TEST_TX_UNSUPPORTED,
	TCP_RUNTIME_TEST_TX_CORE_ERROR
};

struct tcp_runtime_test_tx_result {
	enum tcp_runtime_test_tx_status status;
	size_t length;
	uint32_t actions;
	/* Successful status means serialization into caller storage only. It is not
	 * a device handoff or evidence of packet transmission. */
};

/* Scalar-only observation of a host-test local close. No Core handle, pointer,
 * packet, or serialized output escapes this API. */
struct tcp_runtime_test_close_result {
	enum tcp_runtime_test_close_status status;
	enum tcp_connection_state state;
	uint32_t actions;
	uint32_t snd_una;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint16_t send_window;
	uint16_t receive_window;
	size_t accepted_bytes;
	bool has_child;
	uint64_t timer_deadline;
	uint64_t timer_interval;
	uint8_t retransmissions;
	bool timer_armed;
};

/* Scalar-only host-test result. On active_open success, handle is one retained
 * runtime reference owned by the caller. Segment processing returns no handle
 * and retains no input bytes or parsed pointers. */
struct tcp_runtime_test_active_result {
	enum tcp_runtime_test_active_status status;
	struct tcp_connection_handle handle;
	bool has_reference;
	uint32_t actions;
	enum tcp_connection_state state;
	uint32_t snd_una;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint16_t send_window;
	uint16_t receive_window;
	bool timer_armed;
	size_t accepted_bytes;
	bool has_child;
	uint64_t timer_deadline;
	uint64_t timer_interval;
	uint8_t retransmissions;
};

/* Runtime-owned scalar projection of a Core result. In particular, a Core
 * child handle is never copied out; Runtime's handle field is authoritative. */
struct tcp_runtime_test_core_result {
	uint32_t actions;
	enum tcp_connection_state state;
	uint32_t snd_una;
	uint32_t snd_nxt;
	uint32_t rcv_nxt;
	uint16_t send_window;
	uint16_t receive_window;
	bool timer_armed;
	uint64_t timer_deadline;
	uint64_t timer_interval;
	uint8_t retransmissions;
	size_t accepted_bytes;
	bool has_child;
};

struct tcp_runtime_test_input_result {
	enum tcp_runtime_test_input_status status;
	/* Valid only when has_reference is true; caller releases it through the
	 * Runtime release API. For CHILD_CREATED this is the new child, while
	 * outcome describes the listener's Core event (LISTEN/has_child). */
	struct tcp_connection_handle handle;
	bool has_reference;
	/* State/actions/sequence/windows/timer fields are the Core result for the
	 * event target. Runtime status separately classifies acceptance/establishment. */
	struct tcp_runtime_test_core_result outcome;
};

/* Deterministic passive-open integration exists only in host tests. The seed
 * is never accepted by a production entry point. Returned handles are owned
 * by the caller and must be released. */
enum tcp_runtime_test_input_status tcp_runtime_test_process_segment(
	const uint8_t source_address[4], const uint8_t destination_address[4],
	const struct tcp_wire_segment *segment, uint32_t deterministic_isn,
	uint64_t now_tick, struct tcp_runtime_test_input_result *result);
/* Explicit deterministic ISN injection exists only in the TCP_RUNTIME_TESTING
 * host build. It is never a production entropy fallback. */
enum tcp_runtime_test_active_status tcp_runtime_test_active_open(
	const struct tcp_core_tuple *tuple, uint32_t deterministic_isn,
	uint64_t now_tick, struct tcp_runtime_test_active_result *result);
/* Parses and checksum-validates the raw IPv4 TCP payload, then submits it to
 * Core only for the exact active tuple. The final ACK remains an abstract
 * action and is never transmitted. */
enum tcp_runtime_test_active_status tcp_runtime_test_active_input(
	struct tcp_connection_handle handle, const uint8_t source_address[4],
	const uint8_t destination_address[4], const uint8_t *segment_bytes,
	size_t segment_length, uint64_t now_tick,
	struct tcp_runtime_test_active_result *result);
bool tcp_runtime_test_reserve(const struct tcp_core_tuple *tuple,
	struct tcp_connection_handle *handle);
bool tcp_runtime_test_retain(struct tcp_connection_handle handle);
bool tcp_runtime_test_activate(struct tcp_connection_handle handle,
	uint32_t deterministic_seed,
	struct tcp_runtime_test_core_result *result);
bool tcp_runtime_test_close(struct tcp_connection_handle handle,
	uint64_t now_tick, struct tcp_runtime_test_core_result *result);
enum tcp_runtime_test_close_status tcp_runtime_test_local_close(
	struct tcp_connection_handle handle, uint64_t now_tick,
	struct tcp_runtime_test_close_result *result);
bool tcp_runtime_test_release(struct tcp_connection_handle handle);
bool tcp_runtime_test_snapshot(struct tcp_connection_handle handle,
	struct tcp_runtime_test_core_result *result);
bool tcp_runtime_test_inject_listener_alias(
	struct tcp_connection_handle listener,
	struct tcp_connection_handle *alias);
bool tcp_runtime_test_set_references(struct tcp_connection_handle handle,
	uint16_t references);
bool tcp_runtime_test_reference_count(struct tcp_connection_handle handle,
	uint16_t *references);
bool tcp_runtime_test_set_timer_service_active(bool active);
/* Host simulator context selector. Each index owns a distinct Core table,
 * Runtime lifecycle, relationships, references, and test action metadata.
 * Select only between API calls; contexts are deterministic and serialized,
 * not concurrent or SMP-safe. */
bool tcp_runtime_test_select_instance(size_t instance);
/* Bounded nonblocking stream-style I/O for isolated host Runtime tests only.
 * Writes are all-or-nothing; zero-length write succeeds with zero bytes.
 * Reads may be partial. A zero-capacity read succeeds without consuming data.
 * Empty reads return WOULD_BLOCK until peer FIN (then EOF after buffered bytes)
 * or reset (RESET immediately; Core discards buffered data on RST). */
enum tcp_runtime_test_io_status tcp_runtime_test_write(
	struct tcp_connection_handle handle, const uint8_t *data, size_t length,
	uint64_t now_tick, struct tcp_runtime_test_io_result *result);
enum tcp_runtime_test_io_status tcp_runtime_test_read(
	struct tcp_connection_handle handle, uint8_t *data, size_t capacity,
	struct tcp_runtime_test_io_result *result);
/* Returns one queued serialized TCP segment at a time in caller-owned storage.
 * Too-small output leaves the pending action intact. Success means only that
 * a checksummed TCP segment was constructed; no TX/device ownership transfers.
 * RST actions lacking enough saved input context are reported UNSUPPORTED. */
enum tcp_runtime_test_tx_status tcp_runtime_test_next_segment(
	struct tcp_connection_handle handle, uint8_t *output, size_t capacity,
	struct tcp_runtime_test_tx_result *result);
#endif

#endif
