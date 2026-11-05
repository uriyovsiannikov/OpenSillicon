#include "../include/os.h"
#include "netdiag_args.h"

#define NETDIAG_AF_INET 2U
#define NETDIAG_SOCK_DGRAM 2U
#define NETDIAG_PROTOCOL_UDP 17U
#define NETDIAG_LOCAL_PORT 45000U
#define NETDIAG_ETIMEDOUT 110

static const char request_payload[] = "OS-UDP-REQ-15.3B";
static const char expected_payload[] = "OS-UDP-REPLY-15.3B";

static int
text_is(const char *text, const char *expected)
{
	if (text == NULL)
		return 0;
	while (*expected != '\0')
		if (*text++ != *expected++)
			return 0;
	return *text == '\0';
}

static int
same_bytes(const char *left, const char *right, size_t length)
{
	for (size_t i = 0; i < length; i++)
		if (left[i] != right[i])
			return 0;
	return 1;
}

static void
print_ipv4(const uint8_t address[4])
{
	for (size_t i = 0; i < 4; i++) {
		if (i != 0)
			u_puts(".");
		u_put_u64(address[i]);
	}
}

static void
report_error(const char *operation, long result)
{
	u_puts("netdiag: ");
	u_puts(operation);
	if (result == -NETDIAG_ETIMEDOUT && operation[0] == 'r') {
		u_puts(" timed out (kernel receive bound: 500 ticks, 5 seconds)\n");
		return;
	}
	u_puts(" failed (errno ");
	u_put_u64((uint64_t)(-result));
	u_puts(")\n");
}

static int
udp_diagnostic(const uint8_t peer_address[4], uint16_t peer_port)
{
	const struct u_udp_endpoint local = {
		.address = {0, 0, 0, 0},
		.port = NETDIAG_LOCAL_PORT
	};
	struct u_udp_endpoint destination = {.port = peer_port};
	struct u_udp_endpoint source = {0};
	char reply[64];
	long descriptor;
	long result;
	int status = 1;
	int opened = 0;

	for (size_t i = 0; i < 4; i++)
		destination.address[i] = peer_address[i];
	descriptor = u_udp_socket(NETDIAG_AF_INET, NETDIAG_SOCK_DGRAM,
	    NETDIAG_PROTOCOL_UDP);
	if (descriptor < 0) {
		report_error("socket creation", descriptor);
		return 1;
	}
	opened = 1;
	result = u_udp_bind((uint64_t)descriptor, &local);
	if (result < 0) {
		report_error("bind to local diagnostic port 45000", result);
		goto out;
	}
	result = u_udp_sendto((uint64_t)descriptor, request_payload,
	    sizeof(request_payload) - 1, &destination);
	if (result < 0) {
		report_error("send", result);
		goto out;
	}
	if ((size_t)result != sizeof(request_payload) - 1) {
		u_puts("netdiag: send returned an unexpected datagram length\n");
		goto out;
	}
	u_puts("netdiag: request sent; waiting for one UDP reply\n");
	result = u_udp_recvfrom((uint64_t)descriptor, reply, sizeof(reply), &source);
	if (result < 0) {
		report_error("receive", result);
		goto out;
	}
	if (source.address[0] != peer_address[0] ||
	    source.address[1] != peer_address[1] ||
	    source.address[2] != peer_address[2] ||
	    source.address[3] != peer_address[3] || source.port != peer_port) {
		u_puts("netdiag: reply came from an unexpected peer ");
		print_ipv4(source.address);
		u_puts(":");
		u_put_u64(source.port);
		u_puts("\n");
		goto out;
	}
	if ((size_t)result != sizeof(expected_payload) - 1 ||
	    !same_bytes(reply, expected_payload, sizeof(expected_payload) - 1)) {
		u_puts("netdiag: peer matched, but reply payload/length was unexpected\n");
		goto out;
	}
	u_puts("netdiag: UDP reply verified from ");
	print_ipv4(source.address);
	u_puts(":");
	u_put_u64(source.port);
	u_puts(" (request ");
	u_put_u64(sizeof(request_payload) - 1);
	u_puts(" bytes, reply ");
	u_put_u64((uint64_t)result);
	u_puts(" bytes)\n");
	status = 0;
out:
	if (opened && u_close((uint64_t)descriptor) != 0) {
		u_puts("netdiag: socket close failed\n");
		status = 1;
	}
	return status;
}

static void
print_help(void)
{
	u_puts("usage: netdiag help | capabilities | udp <IPv4> <port>\n");
}

static void
print_capabilities(void)
{
	u_puts("netdiag: UDP through CycloneTCP over the bounded Ethernet/IPv4 stack\n");
	u_puts("netdiag: use ifconfig to inspect the DHCP IPv4 and DNS configuration\n");
	u_puts("netdiag: QEMU user networking gateway is 10.15.3.2;\n");
	u_puts("netdiag: guest address is assigned by DHCP\n");
	u_puts("netdiag: UDP payload limit 512 bytes; receive queue 4 datagrams/socket\n");
	u_puts("netdiag: ICMP Echo, UDP, TCP streams, and DHCP use CycloneTCP\n");
	u_puts("netdiag: IPv4 DNS A queries are available through host/getaddrinfo\n");
	u_puts("netdiag: HTTP clients and TLS are not included in the current userland\n");
}

int
user_main(uint64_t argc, char **argv)
{
	uint8_t peer_address[4];
	uint16_t peer_port;

#ifdef NETDIAG_QEMU_TEST
	if (argc == 0) {
		static const uint8_t test_peer[4] = {10, 15, 3, 2};
		return udp_diagnostic(test_peer, 45001);
	}
#endif
	if (argc == 2 && argv != NULL && text_is(argv[1], "help")) {
		print_help();
		return 0;
	}
	if (argc == 2 && argv != NULL && text_is(argv[1], "capabilities")) {
		print_capabilities();
		return 0;
	}
	if (!netdiag_parse_udp_command((size_t)argc,
	    (const char *const *)argv, peer_address, &peer_port)) {
		print_help();
		return 2;
	}
	return udp_diagnostic(peer_address, peer_port);
}
