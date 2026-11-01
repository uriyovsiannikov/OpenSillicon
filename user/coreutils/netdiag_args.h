#ifndef USER_NETDIAG_ARGS_H
#define USER_NETDIAG_ARGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool netdiag_parse_ipv4(const char *text, uint8_t address[4]);
bool netdiag_parse_port(const char *text, uint16_t *port);
bool netdiag_parse_udp_command(size_t argc, const char *const argv[],
	uint8_t address[4], uint16_t *port);
bool netdiag_parse_ping_command(size_t argc, const char *const argv[],
	uint8_t address[4], uint16_t *count);

#endif
