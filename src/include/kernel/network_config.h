#ifndef KERNEL_NETWORK_CONFIG_H
#define KERNEL_NETWORK_CONFIG_H

#include <stdint.h>

struct ipv4_network_config {
	uint8_t address[4];
	uint8_t netmask[4];
	uint8_t gateway[4];
};

#endif
