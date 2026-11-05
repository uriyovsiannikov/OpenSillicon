#include "../include/os.h"
#include <netdb.h>
#include <netinet/in.h>

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

static void
print_ipv4(const struct sockaddr_in *address)
{
	uint32_t value = ntohl(address->sin_addr.s_addr);
	u_put_u64((value >> 24) & 0xffU);
	u_puts(".");
	u_put_u64((value >> 16) & 0xffU);
	u_puts(".");
	u_put_u64((value >> 8) & 0xffU);
	u_puts(".");
	u_put_u64(value & 0xffU);
}

static void
print_help(void)
{
	u_puts("usage: host help | host <name>\n");
}

int
user_main(uint64_t argc, char **argv)
{
	struct addrinfo hints = {0};
	struct addrinfo *result = NULL;
	int error;
	int found = 0;

	if (argc == 2 && text_is(argv[1], "help")) {
		print_help();
		return 0;
	}
	if (argc != 2 || argv == NULL) {
		print_help();
		return 2;
	}
	hints.ai_family = AF_INET;
	error = getaddrinfo(argv[1], NULL, &hints, &result);
	if (error != 0) {
		u_puts("host: ");
		u_puts(gai_strerror(error));
		u_puts("\n");
		return 1;
	}
	for (struct addrinfo *entry = result; entry != NULL;
	    entry = entry->ai_next) {
		u_puts(argv[1]);
		u_puts(" has address ");
		print_ipv4((const struct sockaddr_in *)entry->ai_addr);
		u_puts("\n");
		found = 1;
	}
	freeaddrinfo(result);
	return found ? 0 : 1;
}
