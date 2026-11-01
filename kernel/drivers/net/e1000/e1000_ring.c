#include "kernel/e1000_ring.h"

bool
e1000_tx_slot_available(uint32_t tail, uint32_t head, size_t count,
	bool in_flight)
{
	if (count == 0 || count > E1000_TX_RING_COUNT || tail >= count ||
	    head >= count || in_flight)
		return false;
	return (tail + 1) % count != head;
}

bool
e1000_tx_descriptor_prepare(struct e1000_tx_descriptor *descriptor,
	uint64_t device_address, uint16_t length)
{
	if (descriptor == NULL || device_address == 0 ||
	    (device_address & 0xfU) != 0 || length < 14 ||
	    length > E1000_MAX_FRAME_SIZE)
		return false;
	*descriptor = (struct e1000_tx_descriptor){
		.buffer_address = device_address,
		.length = length,
		.command = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
		    E1000_TXD_CMD_RS,
		.status = 0
	};
	return true;
}

bool
e1000_tx_completion_valid(uint8_t status)
{
	return (status & E1000_TXD_STAT_DD) != 0;
}

bool
e1000_ring_geometry_valid(uint64_t physical, size_t length,
	size_t descriptor_size, size_t count)
{
	if (physical == 0 || (physical & 0x7fU) != 0 || descriptor_size != 16 ||
	    count == 0 || count > E1000_RX_RING_COUNT ||
	    count > SIZE_MAX / descriptor_size)
		return false;
	return length == count * descriptor_size && (length & 0x7fU) == 0;
}

bool
e1000_rx_completion_valid(uint8_t status, uint8_t errors, uint16_t length)
{
	return (status & (E1000_RXD_STAT_DD | E1000_RXD_STAT_EOP)) ==
	    (E1000_RXD_STAT_DD | E1000_RXD_STAT_EOP) && errors == 0 &&
	    length >= 14 && length <= E1000_MAX_FRAME_SIZE;
}
