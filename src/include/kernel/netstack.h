#ifndef KERNEL_NETSTACK_H
#define KERNEL_NETSTACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETSTACK_ETHERNET_HEADER 14U
#define NETSTACK_IPV4_HEADER 20U
#define NETSTACK_ARP_PACKET 28U
#define NETSTACK_MTU 1500U
#define NETSTACK_MAX_FRAME 1518U
#define NETSTACK_ARP_CACHE_SIZE 8U
#define NETSTACK_ARP_TTL_TICKS 6000U
#define NETSTACK_UDP_MAX_PAYLOAD 512U

struct netstack_config {
	uint8_t mac[6];
	uint8_t address[4];
	uint8_t netmask[4];
	uint8_t gateway[4];
};

struct netstack_arp_entry {
	uint8_t address[4];
	uint8_t mac[6];
	uint64_t updated_at;
	bool valid;
};

struct netstack {
	struct netstack_config config;
	struct netstack_arp_entry arp[NETSTACK_ARP_CACHE_SIZE];
	size_t replacement;
};

enum netstack_event {
	NETSTACK_IGNORED = 0,
	NETSTACK_ARP_REPLY,
	NETSTACK_ARP_RESOLVED,
	NETSTACK_UDP_DATAGRAM,
	NETSTACK_ECHO_REQUEST,
	NETSTACK_ECHO_REPLY,
	NETSTACK_ICMP_ERROR,
	/* TCP wire-valid, but intentionally discarded; not accepted as a flow. */
	NETSTACK_TCP_DISABLED,
	/* TCP wire parser failure classes mapped at the IPv4 boundary. */
	NETSTACK_TCP_INVALID_HEADER,
	NETSTACK_TCP_INVALID_CHECKSUM,
	NETSTACK_TCP_INVALID_OPTIONS
};

enum netstack_ipv4_protocol {
	NETSTACK_IPV4_ICMP,
	NETSTACK_IPV4_TCP,
	NETSTACK_IPV4_UDP,
	NETSTACK_IPV4_UNSUPPORTED
};

struct netstack_udp_view {
	uint8_t source_address[4];
	uint16_t source_port;
	uint16_t destination_port;
	const uint8_t *payload;
	size_t payload_length;
};

/* Borrowed view into the RX frame; valid only until its owner releases it. */
struct netstack_icmp_view {
	uint8_t source_address[4];
	uint8_t type;
	uint8_t code;
	uint16_t identifier;
	uint16_t sequence;
	const uint8_t *payload;
	size_t payload_length;
	uint8_t quoted_source[4];
	uint8_t quoted_destination[4];
	uint8_t quoted_type;
	uint16_t quoted_identifier;
	uint16_t quoted_sequence;
};

bool netstack_init(struct netstack *stack,
	const struct netstack_config *config);
/* Validates a bounded, option-free IPv4 header and classifies its protocol.
 * This does not parse transport headers or dispatch a TCP connection. */
bool netstack_ipv4_classify(const uint8_t *packet, size_t available,
	enum netstack_ipv4_protocol *protocol);
bool netstack_arp_lookup(struct netstack *stack, const uint8_t address[4],
	uint64_t now, uint8_t mac[6]);
/* Resolves a destination to its on-link next hop, using the configured default
 * gateway when the destination is outside the local subnet. */
bool netstack_route_ipv4(const struct netstack *stack,
	const uint8_t destination[4], uint8_t next_hop[4]);
bool netstack_build_arp_request(const struct netstack *stack,
	const uint8_t target[4], uint8_t *frame, size_t capacity,
	size_t *frame_length);
bool netstack_build_echo_request(const struct netstack *stack,
	const uint8_t destination_mac[6], const uint8_t destination[4],
	uint16_t identifier, uint16_t sequence, const uint8_t *payload,
	size_t payload_length, uint8_t *frame, size_t capacity,
	size_t *frame_length);
bool netstack_build_udp_frame(const struct netstack *stack,
	const uint8_t destination_mac[6], const uint8_t destination[4],
	uint16_t source_port, uint16_t destination_port,
	const uint8_t *payload, size_t payload_length,
	uint8_t *frame, size_t capacity, size_t *frame_length);
enum netstack_event netstack_process_frame(struct netstack *stack,
	const uint8_t *frame, size_t frame_length, uint64_t now,
	uint8_t *reply, size_t reply_capacity, size_t *reply_length,
	struct netstack_udp_view *udp);
enum netstack_event netstack_process_frame_ex(struct netstack *stack,
	const uint8_t *frame, size_t frame_length, uint64_t now,
	uint8_t *reply, size_t reply_capacity, size_t *reply_length,
	struct netstack_udp_view *udp, struct netstack_icmp_view *icmp);

#endif
