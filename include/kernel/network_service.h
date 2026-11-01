#ifndef KERNEL_NETWORK_SERVICE_H
#define KERNEL_NETWORK_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#define NETWORK_SERVICE_EVENT_CAPACITY 8U
#define NETWORK_SERVICE_MAX_EVENTS 4U

enum network_service_event {
	NETWORK_SERVICE_POLL_RX = 1
};

struct network_service_result {
	size_t events_processed;
	size_t frames_processed;
	size_t errors;
	size_t events_pending;
	bool busy;
};

bool network_service_init(void);
bool network_service_request(enum network_service_event event);
bool network_service_run(size_t budget, struct network_service_result *result);
bool network_service_quiescent(void);

#endif
