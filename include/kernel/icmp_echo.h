#ifndef KERNEL_ICMP_ECHO_H
#define KERNEL_ICMP_ECHO_H

#include <stddef.h>
#include <stdint.h>

#define ICMP_ECHO_PAYLOAD_LIMIT 32U
#define ICMP_ECHO_TIMEOUT_TICKS 500U

/* Fixed-size, copy-only syscall payload. All address octets are network order. */
struct icmp_echo_request {
	uint8_t destination[4];
	uint16_t identifier;
	uint16_t sequence;
	uint16_t payload_length;
	uint16_t reserved;
	uint8_t payload[ICMP_ECHO_PAYLOAD_LIMIT];
	uint8_t reply_source[4];
	uint64_t elapsed_ticks;
};

_Static_assert(sizeof(struct icmp_echo_request) == 56,
	"ICMP echo syscall structure ABI must remain fixed-size");

/* Runs one bounded project-native IPv4 ICMP Echo transaction. */
int network_icmp_echo(struct icmp_echo_request *request);

#endif
