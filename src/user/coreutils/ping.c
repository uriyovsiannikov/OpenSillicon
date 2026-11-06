#include "../include/os.h"
#include "netdiag_args.h"

#define PING_ETIMEDOUT 110
#define PING_EHOSTUNREACH 113
#define PING_EAGAIN 11

static const uint8_t ping_payload[] = "OS-PHASE15.3A-ECHO";

static int
is_help(const char *text)
{
	static const char expected[] = "help";
	size_t i;
	if (text == NULL)
		return 0;
	for (i = 0; expected[i] != '\0' && text[i] == expected[i]; i++)
		;
	return expected[i] == '\0' && text[i] == '\0';
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

static int
ping_one(const uint8_t destination[4], uint16_t sequence)
{
	struct u_icmp_echo_request request = {0};
	long result;
	for (size_t i = 0; i < 4; i++)
		request.destination[i] = destination[i];
	request.identifier = 0x153a;
	request.sequence = sequence;
	request.payload_length = sizeof(ping_payload) - 1;
	for (size_t i = 0; i < request.payload_length; i++)
		request.payload[i] = ping_payload[i];
	result = u_icmp_echo(&request);
	if (result == -PING_ETIMEDOUT) {
		u_puts("ping: request timed out (500 ticks, 5 seconds)\n");
		return 1;
	}
	if (result == -PING_EHOSTUNREACH) {
		u_puts("ping: no IPv4 route (DHCP lease or gateway unavailable)\n");
		return 1;
	}
	if (result == -PING_EAGAIN) {
		u_puts("ping: another ICMP Echo request is already active\n");
		return 1;
	}
	if (result < 0) {
		u_puts("ping: ICMP Echo syscall failed (errno ");
		u_put_u64((uint64_t)-result);
		u_puts(")\n");
		return 1;
	}
	for (size_t i = 0; i < 4; i++)
		if (request.reply_source[i] != destination[i]) {
			u_puts("ping: kernel returned an unexpected reply source\n");
			return 1;
		}
	u_puts("ping: reply from ");
	print_ipv4(request.reply_source);
	u_puts(" sequence=");
	u_put_u64(sequence);
	u_puts(" elapsed=");
	u_put_u64(request.elapsed_ticks);
	u_puts(" ticks\n");
	return 0;
}

static void
print_help(void)
{
	u_puts("usage: ping help | ping <IPv4> [count 1-4]\n");
}

int
user_main(uint64_t argc, char **argv)
{
	uint8_t destination[4];
	uint16_t count;
	int failures = 0;
#ifdef PING_QEMU_TEST
	if (argc == 0) {
		static const uint8_t peer[4] = {10, 15, 3, 2};
		return ping_one(peer, 1);
	}
#endif
	if (argc == 2 && argv != NULL && is_help(argv[1])) {
		print_help();
		return 0;
	}
	if (!netdiag_parse_ping_command((size_t)argc,
	    (const char *const *)argv, destination, &count)) {
		print_help();
		return 2;
	}
	for (uint16_t sequence = 1; sequence <= count; sequence++)
		failures += ping_one(destination, sequence);
	u_puts("ping: ");
	u_put_u64(count - (uint16_t)failures);
	u_puts(" replies, ");
	u_put_u64((uint16_t)failures);
	u_puts(" failures\n");
	return failures == 0 ? 0 : 1;
}
