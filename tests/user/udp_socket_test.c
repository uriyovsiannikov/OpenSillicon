#include "include/os.h"

#define UDP_TEST_AF_INET 2U
#define UDP_TEST_SOCK_DGRAM 2U
#define UDP_TEST_PROTOCOL 17U
#define UDP_TEST_LOCAL_PORT 45000U
#define UDP_TEST_PEER_PORT 45001U
#define UDP_TEST_EAFNOSUPPORT 97
#define UDP_TEST_EADDRINUSE 98
#define UDP_TEST_ENOTSOCK 88

static int
same_bytes(const char *left, const char *right, size_t length)
{
	for (size_t i = 0; i < length; i++)
		if (left[i] != right[i])
			return 0;
	return 1;
}

long
user_main(uint64_t argc, char **argv, char **envp)
{
	static const char request[] = "OS-UDP-REQ-15.3B";
	static const char expected[] = "OS-UDP-REPLY-15.3B";
	const struct u_udp_endpoint local = {
		.address = {10, 15, 3, 1},
		.port = UDP_TEST_LOCAL_PORT
	};
	const struct u_udp_endpoint destination = {
		.address = {10, 15, 3, 2},
		.port = UDP_TEST_PEER_PORT
	};
	struct u_udp_endpoint source = {0};
	char reply[64];
	long descriptor;
	long duplicate;
	long received;

	(void)argv;
	(void)envp;
	if (argc != 0)
		return 1;
	if (u_udp_socket(99, UDP_TEST_SOCK_DGRAM, UDP_TEST_PROTOCOL) !=
	    -UDP_TEST_EAFNOSUPPORT || u_udp_bind(1, &local) !=
	    -UDP_TEST_ENOTSOCK)
		return 2;
	descriptor = u_udp_socket(UDP_TEST_AF_INET, UDP_TEST_SOCK_DGRAM,
	    UDP_TEST_PROTOCOL);
	if (descriptor < 0)
		return 3;
	if (u_udp_bind((uint64_t)descriptor, &local) != 0) {
		(void)u_close((uint64_t)descriptor);
		return 4;
	}
	duplicate = u_udp_socket(UDP_TEST_AF_INET, UDP_TEST_SOCK_DGRAM, 0);
	if (duplicate < 0 || u_udp_bind((uint64_t)duplicate, &local) !=
	    -UDP_TEST_EADDRINUSE) {
		if (duplicate >= 0)
			(void)u_close((uint64_t)duplicate);
		(void)u_close((uint64_t)descriptor);
		return 5;
	}
	(void)u_close((uint64_t)duplicate);
	if (u_udp_sendto((uint64_t)descriptor, request, sizeof(request) - 1,
	    &destination) != (long)(sizeof(request) - 1)) {
		(void)u_close((uint64_t)descriptor);
		return 6;
	}
	u_puts("OpenSillicon: user UDP datagram sent\n");
	received = u_udp_recvfrom((uint64_t)descriptor, reply, sizeof(reply),
	    &source);
	if (received != (long)(sizeof(expected) - 1) ||
	    !same_bytes(reply, expected, sizeof(expected) - 1) ||
	    source.address[0] != 10 || source.address[1] != 15 ||
	    source.address[2] != 3 || source.address[3] != 2 ||
	    source.port != UDP_TEST_PEER_PORT) {
		(void)u_close((uint64_t)descriptor);
		return 7;
	}
	if (u_close((uint64_t)descriptor) != 0)
		return 8;
	u_puts("OpenSillicon: user UDP reply payload/source verified\n");
	return 0;
}
