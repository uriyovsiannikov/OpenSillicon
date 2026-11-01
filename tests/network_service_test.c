#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "kernel/network_service.h"

static size_t poll_calls;
static bool try_recursive_service;
static struct network_service_result recursive_result;
static int next_poll_result = 1;

int
udp_socket_poll(void)
{
	int result = next_poll_result;
	next_poll_result = 1;
	poll_calls++;
	if (try_recursive_service) {
		try_recursive_service = false;
		assert(network_service_run(1, &recursive_result));
	}
	return result;
}

int
main(void)
{
	struct network_service_result result;
	assert(!network_service_request(NETWORK_SERVICE_POLL_RX));
	assert(network_service_init());
	assert(!network_service_init());
	assert(network_service_quiescent());

	for (size_t i = 0; i < NETWORK_SERVICE_EVENT_CAPACITY; i++)
		assert(network_service_request(NETWORK_SERVICE_POLL_RX));
	assert(!network_service_request(NETWORK_SERVICE_POLL_RX));
	assert(!network_service_quiescent());

	assert(network_service_run(2, &result));
	assert(result.events_processed == 2 && result.frames_processed == 2);
	assert(result.errors == 0 && result.events_pending == 6 && !result.busy);
	assert(poll_calls == 2);

	/* A reentrant timer/syscall service attempt cannot become a second RX owner. */
	try_recursive_service = true;
	assert(network_service_run(1, &result));
	assert(result.events_processed == 1 && result.frames_processed == 1);
	assert(recursive_result.busy && recursive_result.events_processed == 0);
	assert(recursive_result.events_pending == 5);

	assert(network_service_run(99, &result));
	assert(result.events_processed == NETWORK_SERVICE_MAX_EVENTS);
	assert(result.frames_processed == NETWORK_SERVICE_MAX_EVENTS);
	assert(result.events_pending == 1);
	assert(network_service_run(NETWORK_SERVICE_MAX_EVENTS, &result));
	assert(result.events_processed == 1 && result.events_pending == 0);
	assert(poll_calls == NETWORK_SERVICE_EVENT_CAPACITY);
	assert(network_service_quiescent());

	assert(network_service_request(NETWORK_SERVICE_POLL_RX));
	assert(network_service_run(0, &result));
	assert(result.events_processed == 0 && result.events_pending == 1);
	assert(network_service_run(1, &result));
	assert(result.events_processed == 1 && result.events_pending == 0);
	assert(network_service_quiescent());

	/* A poll error is reported and must not leave the service single-flight
	 * guard held; a later request can still make progress. */
	assert(network_service_request(NETWORK_SERVICE_POLL_RX));
	next_poll_result = -5; /* -EIO */
	assert(network_service_run(1, &result));
	assert(result.events_processed == 1 && result.errors == 1 &&
	    result.frames_processed == 0 && result.events_pending == 0);
	assert(network_service_quiescent());
	assert(network_service_request(NETWORK_SERVICE_POLL_RX));
	assert(network_service_run(1, &result));
	assert(result.events_processed == 1 && result.frames_processed == 1 &&
	    result.errors == 0);
	assert(network_service_quiescent());
	puts("Network service queue, four-event budget, progress, and reentrancy tests passed");
	return 0;
}
