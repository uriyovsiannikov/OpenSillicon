#ifndef KERNEL_E1000_RING_H
#define KERNEL_E1000_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define E1000_TX_RING_COUNT 8U
#define E1000_RX_RING_COUNT 8U
#define E1000_DMA_BUFFER_SIZE 2048U
#define E1000_MAX_FRAME_SIZE 1518U
#define E1000_STOP_POLL_LIMIT 1000000U

#define E1000_TXD_CMD_EOP 0x01U
#define E1000_TXD_CMD_IFCS 0x02U
#define E1000_TXD_CMD_RS 0x08U
#define E1000_TXD_STAT_DD 0x01U
#define E1000_RXD_STAT_DD 0x01U
#define E1000_RXD_STAT_EOP 0x02U

struct e1000_tx_descriptor {
	uint64_t buffer_address;
	uint16_t length;
	uint8_t checksum_offset;
	uint8_t command;
	uint8_t status;
	uint8_t checksum_start;
	uint16_t special;
} __attribute__((packed));

struct e1000_rx_descriptor {
	uint64_t buffer_address;
	uint16_t length;
	uint16_t checksum;
	uint8_t status;
	uint8_t errors;
	uint16_t special;
} __attribute__((packed));

_Static_assert(sizeof(struct e1000_tx_descriptor) == 16,
	"82540EM legacy TX descriptor must be 16 bytes");
_Static_assert(sizeof(struct e1000_rx_descriptor) == 16,
	"82540EM legacy RX descriptor must be 16 bytes");

bool e1000_ring_geometry_valid(uint64_t physical, size_t length,
	size_t descriptor_size, size_t count);
bool e1000_tx_slot_available(uint32_t tail, uint32_t head, size_t count,
	bool in_flight);
bool e1000_tx_descriptor_prepare(struct e1000_tx_descriptor *descriptor,
	uint64_t device_address, uint16_t length);
bool e1000_tx_completion_valid(uint8_t status);
bool e1000_rx_completion_valid(uint8_t status, uint8_t errors,
	uint16_t length);

#endif
