#include <assert.h>
#include <stdio.h>

#include "kernel/tcp_runtime.h"

int
main(void)
{
	static const uint8_t source[4] = {10, 15, 3, 2};
	static const uint8_t destination[4] = {10, 15, 3, 1};
	struct tcp_wire_segment segment = {
		.source_port = 40000,
		.destination_port = 443,
		.data_offset = 5
	};
	struct tcp_runtime_route_result result;
	assert(tcp_runtime_init());
	assert(tcp_runtime_route_segment(source, destination, &segment, &result) ==
	    TCP_RUNTIME_ROUTE_DISABLED);
	assert(result.status == TCP_RUNTIME_ROUTE_DISABLED &&
	    !result.has_reference && !result.event_submitted);
	assert(tcp_runtime_shutdown());
	puts("TCP runtime default build: routing gate disabled; no tuple lookup or event submission PASS");
	return 0;
}
