#include "netdiag_args.h"

static bool
text_is(const char *text, const char *expected)
{
	if (text == NULL)
		return false;
	while (*expected != '\0')
		if (*text++ != *expected++)
			return false;
	return *text == '\0';
}

bool
netdiag_parse_ipv4(const char *text, uint8_t address[4])
{
	const char *cursor = text;
	if (cursor == NULL || address == NULL)
		return false;
	for (size_t part = 0; part < 4; part++) {
		unsigned int value = 0;
		size_t digits = 0;
		while (*cursor >= '0' && *cursor <= '9') {
			value = value * 10U + (unsigned int)(*cursor++ - '0');
			if (value > 255U)
				return false;
			digits++;
		}
		if (digits == 0 || (part < 3 && *cursor++ != '.') ||
		    (part == 3 && *cursor != '\0'))
			return false;
		address[part] = (uint8_t)value;
	}
	return true;
}

bool
netdiag_parse_port(const char *text, uint16_t *port)
{
	unsigned int value = 0;
	if (text == NULL || port == NULL || *text == '\0')
		return false;
	for (const char *cursor = text; *cursor != '\0'; cursor++) {
		if (*cursor < '0' || *cursor > '9')
			return false;
		value = value * 10U + (unsigned int)(*cursor - '0');
		if (value > UINT16_MAX)
			return false;
	}
	if (value == 0)
		return false;
	*port = (uint16_t)value;
	return true;
}

bool
netdiag_parse_udp_command(size_t argc, const char *const argv[],
	uint8_t address[4], uint16_t *port)
{
	return argc == 4 && argv != NULL && address != NULL && port != NULL &&
	    argv[1] != NULL && argv[2] != NULL && argv[3] != NULL &&
	    text_is(argv[1], "udp") &&
	    netdiag_parse_ipv4(argv[2], address) &&
	    netdiag_parse_port(argv[3], port);
}

bool
netdiag_parse_ping_command(size_t argc, const char *const argv[],
	uint8_t address[4], uint16_t *count)
{
	uint16_t parsed_count = 1;
	if (argv == NULL || address == NULL || count == NULL ||
	    (argc != 2 && argc != 3) || argv[1] == NULL ||
	    !netdiag_parse_ipv4(argv[1], address))
		return false;
	if (argc == 3 && !netdiag_parse_port(argv[2], &parsed_count))
		return false;
	if (parsed_count > 4)
		return false;
	*count = parsed_count;
	return true;
}
