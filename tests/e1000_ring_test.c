#include <stdio.h>

#include "kernel/e1000_ring.h"

int
main(void)
{
	struct e1000_tx_descriptor tx = {0};
	if (!e1000_ring_geometry_valid(0x1000, 128, 16, 8) ||
	    e1000_ring_geometry_valid(0x1040, 128, 16, 8) ||
	    e1000_ring_geometry_valid(0x1000, 127, 16, 8) ||
	    e1000_ring_geometry_valid(0x1000, 256, 16, 8) ||
	    e1000_ring_geometry_valid(0x1000, 128, 8, 8) ||
	    e1000_ring_geometry_valid(0x1000, 128, 16, 9) ||
	    !e1000_tx_slot_available(0, 0, 8, false) ||
	    e1000_tx_slot_available(7, 0, 8, false) ||
	    !e1000_tx_slot_available(7, 1, 8, false) ||
	    e1000_tx_slot_available(0, 0, 8, true) ||
	    e1000_tx_slot_available(8, 0, 8, false) ||
	    !e1000_tx_descriptor_prepare(&tx, 0x2000, 60) ||
	    tx.buffer_address != 0x2000 || tx.length != 60 || tx.status != 0 ||
	    tx.command != (E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS |
	    E1000_TXD_CMD_RS) ||
	    e1000_tx_descriptor_prepare(&tx, 0x2008, 60) ||
	    e1000_tx_descriptor_prepare(&tx, 0x2000, 0) ||
	    e1000_tx_descriptor_prepare(&tx, 0x2000,
	    E1000_MAX_FRAME_SIZE + 1) ||
	    !e1000_tx_completion_valid(E1000_TXD_STAT_DD) ||
	    e1000_tx_completion_valid(0) ||
	    e1000_rx_completion_valid(E1000_RXD_STAT_DD |
	    E1000_RXD_STAT_EOP, 0, 60) == false ||
	    e1000_rx_completion_valid(E1000_RXD_STAT_DD, 0, 60) ||
	    e1000_rx_completion_valid(E1000_RXD_STAT_DD |
	    E1000_RXD_STAT_EOP, 1, 60) ||
	    e1000_rx_completion_valid(E1000_RXD_STAT_DD |
	    E1000_RXD_STAT_EOP, 0, 0) ||
	    e1000_rx_completion_valid(E1000_RXD_STAT_DD |
	    E1000_RXD_STAT_EOP, 0, E1000_MAX_FRAME_SIZE + 1)) {
		fprintf(stderr, "e1000 descriptor geometry/completion validation failed\n");
		return 1;
	}
	puts("e1000 82540EM TX/RX descriptor bounds, slot, and completion tests passed");
	return 0;
}
