#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "coreutils/netdiag_args.h"

static bool
text_equal(const char *text, const char *expected)
{
	if (text == NULL)
		return false;
	while (*expected != '\0') {
		if (*text++ != *expected++)
			return false;
	}
	return *text == '\0';
}

bool
netdiag_parse_ipv4(const char *text, uint8_t address[4])
{
	uint8_t parsed[4];
	const char *cursor = text;

	if (text == NULL || address == NULL)
		return false;
	for (size_t octet = 0; octet < 4; octet++) {
		unsigned int value = 0;
		unsigned int digits = 0;

		while (*cursor >= '0' && *cursor <= '9') {
			if (++digits > 3)
				return false;
			value = value * 10U + (unsigned int)(*cursor - '0');
			if (value > 255U)
				return false;
			cursor++;
		}
		if (digits == 0)
			return false;
		parsed[octet] = (uint8_t)value;
		if (octet != 3) {
			if (*cursor != '.')
				return false;
			cursor++;
		}
	}
	if (*cursor != '\0')
		return false;
	if (parsed[0] == 0 || parsed[0] >= 224 ||
	    (parsed[0] == 255 && parsed[1] == 255 && parsed[2] == 255 &&
	    parsed[3] == 255))
		return false;
	for (size_t i = 0; i < 4; i++)
		address[i] = parsed[i];
	return true;
}

bool
netdiag_parse_port(const char *text, uint16_t *port)
{
	uint32_t value = 0;
	const char *cursor = text;

	if (text == NULL || port == NULL || *cursor == '\0')
		return false;
	while (*cursor >= '0' && *cursor <= '9') {
		value = value * 10U + (uint32_t)(*cursor - '0');
		if (value > UINT16_MAX)
			return false;
		cursor++;
	}
	if (*cursor != '\0' || value == 0)
		return false;
	*port = (uint16_t)value;
	return true;
}

bool
netdiag_parse_udp_command(size_t argc, const char *const argv[],
	uint8_t address[4], uint16_t *port)
{
	uint8_t parsed_address[4];
	uint16_t parsed_port;

	if (argc != 4 || argv == NULL || address == NULL || port == NULL ||
	    !text_equal(argv[0], "netdiag") || !text_equal(argv[1], "udp") ||
	    argv[2] == NULL || argv[3] == NULL ||
	    !netdiag_parse_ipv4(argv[2], parsed_address) ||
	    !netdiag_parse_port(argv[3], &parsed_port))
		return false;
	for (size_t i = 0; i < 4; i++)
		address[i] = parsed_address[i];
	*port = parsed_port;
	return true;
}

bool
netdiag_parse_ping_command(size_t argc, const char *const argv[],
	uint8_t address[4], uint16_t *count)
{
	uint8_t parsed_address[4];
	uint16_t parsed_count = 1;
	if ((argc != 2 && argc != 3) || argv == NULL || address == NULL ||
	    count == NULL || !text_equal(argv[0], "ping") || argv[1] == NULL ||
	    !netdiag_parse_ipv4(argv[1], parsed_address))
		return false;
	if (argc == 3 && (!netdiag_parse_port(argv[2], &parsed_count) ||
	    parsed_count > 4))
		return false;
	for (size_t i = 0; i < 4; i++)
		address[i] = parsed_address[i];
	*count = parsed_count;
	return true;
}
