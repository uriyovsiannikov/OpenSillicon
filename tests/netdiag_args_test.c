#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "coreutils/netdiag_args.h"

static void
test_ipv4(void)
{
	uint8_t address[4] = {9, 9, 9, 9};
	assert(netdiag_parse_ipv4("10.15.3.2", address));
	assert(address[0] == 10 && address[1] == 15 && address[2] == 3 &&
	    address[3] == 2);
	assert(!netdiag_parse_ipv4(NULL, address));
	assert(!netdiag_parse_ipv4("", address));
	assert(!netdiag_parse_ipv4("10.15.3", address));
	assert(!netdiag_parse_ipv4("10..3.2", address));
	assert(!netdiag_parse_ipv4("10.15.3.2.1", address));
	assert(!netdiag_parse_ipv4("10.15.3.256", address));
	assert(!netdiag_parse_ipv4("1000.1.1.1", address));
	assert(!netdiag_parse_ipv4("10.15.3.-1", address));
	assert(!netdiag_parse_ipv4("10.15.3.2x", address));
	assert(!netdiag_parse_ipv4("0.0.0.0", address));
	assert(!netdiag_parse_ipv4("224.0.0.1", address));
	assert(!netdiag_parse_ipv4("255.255.255.255", address));
	assert(!netdiag_parse_ipv4("10.15.3.2", NULL));
}

static void
test_port_and_command(void)
{
	uint16_t port = 0;
	uint8_t address[4];
	const char *valid[] = {"netdiag", "udp", "10.15.3.2", "65535"};
	const char *zero[] = {"netdiag", "udp", "10.15.3.2", "0"};
	const char *too_large[] = {"netdiag", "udp", "10.15.3.2", "65536"};
	const char *trailing[] = {"netdiag", "udp", "10.15.3.2", "53x"};
	const char *ping_one[] = {"ping", "10.15.3.2"};
	const char *ping_many[] = {"ping", "10.15.3.2", "4"};
	const char *ping_zero[] = {"ping", "10.15.3.2", "0"};
	const char *ping_too_many[] = {"ping", "10.15.3.2", "5"};
	const char *ping_bad_ip[] = {"ping", "10.15.3"};
	assert(netdiag_parse_port("1", &port) && port == 1);
	assert(netdiag_parse_port("65535", &port) && port == UINT16_MAX);
	assert(!netdiag_parse_port("0", &port));
	assert(!netdiag_parse_port("65536", &port));
	assert(!netdiag_parse_port("", &port));
	assert(!netdiag_parse_port("+53", &port));
	assert(!netdiag_parse_port("53x", &port));
	assert(!netdiag_parse_port("999999999999999999999999", &port));
	assert(!netdiag_parse_port("53", NULL));
	assert(netdiag_parse_udp_command(4, valid, address, &port));
	assert(port == 65535 && address[0] == 10 && address[3] == 2);
	assert(!netdiag_parse_udp_command(4, zero, address, &port));
	assert(!netdiag_parse_udp_command(4, too_large, address, &port));
	assert(!netdiag_parse_udp_command(4, trailing, address, &port));
	assert(!netdiag_parse_udp_command(3, valid, address, &port));
	assert(!netdiag_parse_udp_command(5, valid, address, &port));
	assert(!netdiag_parse_udp_command(4, NULL, address, &port));
	assert(!netdiag_parse_udp_command(4, valid, NULL, &port));
	assert(!netdiag_parse_udp_command(4, valid, address, NULL));
	uint16_t count = 0;
	assert(netdiag_parse_ping_command(2, ping_one, address, &count) &&
	    count == 1 && address[3] == 2);
	assert(netdiag_parse_ping_command(3, ping_many, address, &count) &&
	    count == 4);
	assert(!netdiag_parse_ping_command(3, ping_zero, address, &count));
	assert(!netdiag_parse_ping_command(3, ping_too_many, address, &count));
	assert(!netdiag_parse_ping_command(2, ping_bad_ip, address, &count));
	assert(!netdiag_parse_ping_command(2, NULL, address, &count));
	assert(!netdiag_parse_ping_command(2, ping_one, NULL, &count));
	assert(!netdiag_parse_ping_command(2, ping_one, address, NULL));
}

int
main(void)
{
	test_ipv4();
	test_port_and_command();
	puts("netdiag/ping IPv4, port, count, and command validation tests passed");
	return 0;
}
