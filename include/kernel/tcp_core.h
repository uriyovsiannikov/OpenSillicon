#ifndef KERNEL_TCP_CORE_H
#define KERNEL_TCP_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "kernel/tcp_wire.h"

/* Experimental deterministic core only: its timer service is isolated and
 * unwired; there is no live network caller, socket API, or production ISN
 * source. */

#define TCP_CORE_CONNECTION_LIMIT 8U
#define TCP_CORE_LISTEN_BACKLOG 4U
#define TCP_CORE_RX_CAPACITY 2048U
#define TCP_CORE_MAX_SEGMENT 1480U
#define TCP_CORE_LOCAL_MSS 1460U
#define TCP_CORE_DEFAULT_MSS 536U
#define TCP_CORE_SEND_WINDOW_LIMIT 2048U
#define TCP_CORE_RETRY_LIMIT 5U
#define TCP_CORE_TICK_HZ 100U
#define TCP_CORE_RTO_INITIAL_TICKS 100U
#define TCP_CORE_RTO_MAX_TICKS 800U
#define TCP_CORE_TIME_WAIT_TICKS 12000U
#define TCP_CORE_SERVICE_EVENT_LIMIT 4U

#define TCP_CORE_LIVE_ISN_AVAILABLE 0

#define TCP_ACTION_SEND_SYN (1U << 0)
#define TCP_ACTION_SEND_ACK (1U << 1)
#define TCP_ACTION_SEND_FIN (1U << 2)
#define TCP_ACTION_SEND_RST (1U << 3)
#define TCP_ACTION_RETRANSMIT (1U << 4)
#define TCP_ACTION_DELIVER_DATA (1U << 5)
#define TCP_ACTION_DUPLICATE_ACK (1U << 6)
#define TCP_ACTION_ENTER_TIME_WAIT (1U << 7)
#define TCP_ACTION_ABORT (1U << 8)

struct tcp_core_tuple {
	uint8_t local_address[4];
	uint8_t remote_address[4];
	uint16_t local_port;
	uint16_t remote_port;
};

enum tcp_core_lookup_status {
	TCP_CORE_LOOKUP_FOUND = 0,
	TCP_CORE_LOOKUP_NOT_FOUND,
	TCP_CORE_LOOKUP_AMBIGUOUS,
	TCP_CORE_LOOKUP_REFERENCE_LIMIT,
	TCP_CORE_LOOKUP_INVALID
};

struct tcp_segment_view {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence;
	uint32_t acknowledgment;
	uint8_t flags;
	uint16_t window;
	bool mss_present;
	uint16_t mss;
	/* Borrowed view into the input segment; never retained after tcp_core_step. */
	const uint8_t *payload;
	size_t payload_length;
};

struct tcp_segment_spec {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence;
	uint32_t acknowledgment;
	uint8_t flags;
	uint16_t window;
	bool include_mss;
	uint16_t mss;
	const uint8_t *payload;
	size_t payload_length;
};

enum tcp_connection_state {
	TCP_STATE_CLOSED = 0,
	TCP_STATE_LISTEN,
	TCP_STATE_SYN_SENT,
	TCP_STATE_SYN_RECEIVED,
	TCP_STATE_ESTABLISHED,
	TCP_STATE_FIN_WAIT_1,
	TCP_STATE_FIN_WAIT_2,
	TCP_STATE_CLOSE_WAIT,
	TCP_STATE_LAST_ACK,
	TCP_STATE_CLOSING,
	TCP_STATE_TIME_WAIT
};

struct tcp_connection_handle {
	/* Core table slot token. Runtime APIs use this storage shape as an opaque
	 * Runtime token; callers must not pass Runtime-owned tokens to Core APIs. */
	uint8_t slot;
	uint32_t generation;
};

struct tcp_core_connection {
	bool allocated;
	bool table_owned;
	bool has_listener;
	bool started;
	uint16_t external_references;
	uint32_t generation;
	uint32_t listener_generation;
	uint8_t listener_slot;
	enum tcp_connection_state state;
	struct tcp_core_tuple tuple;
	uint32_t iss;
	uint32_t irs;
	uint32_t snd_una;
	uint32_t snd_nxt;
	uint32_t outstanding_end;
	uint32_t rcv_nxt;
	uint16_t snd_wnd;
	uint16_t peer_mss;
	uint8_t outstanding_flags;
	uint8_t retransmissions;
	uint8_t duplicate_acks;
	bool outstanding_control;
	bool timer_armed;
	uint64_t retransmit_deadline;
	uint64_t retransmit_interval;
	uint64_t time_wait_deadline;
	uint8_t rx_data[TCP_CORE_RX_CAPACITY];
	size_t rx_head;
	size_t rx_length;
};

struct tcp_connection_table {
	struct tcp_core_connection slots[TCP_CORE_CONNECTION_LIMIT];
	uint32_t next_generation;
};

enum tcp_core_event_type {
	/* Starting a passive listener has no ISN and emits no packet. */
	TCP_EVENT_LISTEN = 0,
#ifdef TCP_CORE_TESTING
	/* Deterministic connection creation exists only in isolated host tests. */
	TCP_EVENT_ACTIVE_OPEN = 1,
#endif
	TCP_EVENT_SEGMENT = 2,
	TCP_EVENT_APPLICATION_CLOSE = 3,
	TCP_EVENT_RETRANSMIT_TIMEOUT = 4,
	TCP_EVENT_TIME_WAIT_EXPIRE = 5
};

struct tcp_core_event {
	enum tcp_core_event_type type;
	/* This deterministic seed field is absent from production builds. */
#ifdef TCP_CORE_TESTING
	uint32_t initial_sequence;
#endif
	uint64_t now_tick;
	uint8_t source_address[4];
	uint8_t destination_address[4];
	const struct tcp_segment_view *segment;
};

struct tcp_core_result {
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
	/* Core-owned caller reference returned only when has_child is true. The
	 * receiving Core caller must release this handle; Runtime may transfer that
	 * reference into a private relationship instead of exposing it externally. */
	struct tcp_connection_handle child;
};

struct tcp_core_service_item {
	struct tcp_connection_handle handle;
	struct tcp_core_result result;
};

struct tcp_core_service_result {
	size_t count;
	bool more_due;
	struct tcp_core_service_item items[TCP_CORE_SERVICE_EVENT_LIMIT];
};

void tcp_core_table_init(struct tcp_connection_table *table);
/* Reserves one unstarted slot and returns one caller-owned Core reference.
 * The reservation is not table-owned yet: releasing its last external
 * reference while still unstarted reclaims it. A successful LISTEN,
 * ACTIVE_OPEN, or passive-child start establishes table ownership; Core drops
 * that ownership on its defined transition to CLOSED. */
bool tcp_core_connection_create(struct tcp_connection_table *table,
	const struct tcp_core_tuple *tuple, struct tcp_connection_handle *handle);
/* Looks up an active exact four-field tuple first, then a listener whose local
 * address is exact or 0.0.0.0. Same-port wildcard/specific listeners conflict
 * at creation. FOUND returns a retained handle; caller must release it. */
enum tcp_core_lookup_status tcp_core_connection_lookup(
	struct tcp_connection_table *table, const struct tcp_core_tuple *tuple,
	struct tcp_connection_handle *handle);
/* Copies the tuple for a valid handle. Caller must hold an external
 * reference and serialize this access with every other table operation. */
bool tcp_core_connection_get_tuple(const struct tcp_connection_table *table,
	struct tcp_connection_handle handle, struct tcp_core_tuple *tuple);
/* Adds one external reference. Caller must balance it with release(). */
bool tcp_core_connection_retain(struct tcp_connection_table *table,
	struct tcp_connection_handle handle);
/* Releases one external Core reference, not table ownership. A stale token or
 * underflow is rejected; generation advances only when the entry is reclaimed. */
bool tcp_core_connection_release(struct tcp_connection_table *table,
	struct tcp_connection_handle handle);
bool tcp_core_connection_get(const struct tcp_connection_table *table,
	struct tcp_connection_handle handle, struct tcp_core_result *snapshot);
size_t tcp_core_connection_count(const struct tcp_connection_table *table);
bool tcp_core_connection_read(struct tcp_connection_table *table,
	struct tcp_connection_handle handle, uint8_t *data, size_t capacity,
	size_t *length);
bool tcp_core_step(struct tcp_connection_table *table,
	struct tcp_connection_handle handle, const struct tcp_core_event *event,
	struct tcp_core_result *result);
/* Caller must serialize this API with other table access. Processes at most
 * budget due control/TIME-WAIT timers. Each returned item owns one external
 * handle reference; the caller must release it. */
bool tcp_core_service(struct tcp_connection_table *table, uint64_t now_tick,
	size_t budget, struct tcp_core_service_result *result);
bool tcp_core_live_isn_available(void);

bool tcp_segment_parse_ipv4(const uint8_t source[4],
	const uint8_t destination[4], const uint8_t *segment, size_t length,
	struct tcp_segment_view *view);
/* Validates the same bounded wire profile/checksum without returning a view.
 * This may reject disabled protocol traffic but never dispatches TCP state. */
bool tcp_segment_validate_ipv4(const uint8_t source[4],
	const uint8_t destination[4], const uint8_t *segment, size_t length);
bool tcp_segment_build_ipv4(const uint8_t source[4],
	const uint8_t destination[4], const struct tcp_segment_spec *spec,
	uint8_t *segment, size_t capacity, size_t *length);

#endif
