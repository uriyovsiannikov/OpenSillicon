#include <stdbool.h>
#include <stdint.h>

#include "kernel/errno.h"
#include "kernel/network_service.h"
#include "kernel/udp_socket.h"

static enum network_service_event event_queue[NETWORK_SERVICE_EVENT_CAPACITY];
static size_t event_head;
static size_t event_count;
static bool initialized;
static bool service_running;

static uint64_t
service_guard_enter(void)
{
#ifdef OPEN_SILLICON_KERNEL
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
#else
	return 0;
#endif
}

static void
service_guard_leave(uint64_t flags)
{
#ifdef OPEN_SILLICON_KERNEL
	if ((flags & (1ULL << 9)) != 0)
		__asm__ volatile ("sti" : : : "memory");
#else
	(void)flags;
#endif
}

bool
network_service_init(void)
{
	uint64_t flags = service_guard_enter();
	bool success = !initialized;
	if (success) {
		event_head = 0;
		event_count = 0;
		service_running = false;
		initialized = true;
	}
	service_guard_leave(flags);
	return success;
}

bool
network_service_request(enum network_service_event event)
{
	uint64_t flags = service_guard_enter();
	if (!initialized || event != NETWORK_SERVICE_POLL_RX ||
	    event_count == NETWORK_SERVICE_EVENT_CAPACITY) {
		service_guard_leave(flags);
		return false;
	}
	event_queue[(event_head + event_count) %
	    NETWORK_SERVICE_EVENT_CAPACITY] = event;
	event_count++;
	service_guard_leave(flags);
	return true;
}

bool
network_service_run(size_t budget, struct network_service_result *result)
{
	struct network_service_result local = {0};
	uint64_t flags = service_guard_enter();
	if (result != NULL)
		*result = local;
	if (!initialized) {
		service_guard_leave(flags);
		return false;
	}
	if (service_running) {
		local.busy = true;
		local.events_pending = event_count;
		if (result != NULL)
			*result = local;
		service_guard_leave(flags);
		return true;
	}
	if (budget > NETWORK_SERVICE_MAX_EVENTS)
		budget = NETWORK_SERVICE_MAX_EVENTS;
	service_running = true;
	service_guard_leave(flags);

	while (local.events_processed < budget) {
		enum network_service_event event;
		int poll_result;

		flags = service_guard_enter();
		if (event_count == 0) {
			service_guard_leave(flags);
			break;
		}
		event = event_queue[event_head];
		event_head = (event_head + 1) % NETWORK_SERVICE_EVENT_CAPACITY;
		event_count--;
		service_guard_leave(flags);

		local.events_processed++;
		if (event != NETWORK_SERVICE_POLL_RX) {
			local.errors++;
			continue;
		}
		poll_result = udp_socket_poll();
		if (poll_result > 0)
			local.frames_processed++;
		else if (poll_result < 0 && poll_result != -ENETDOWN)
			local.errors++;
	}

	flags = service_guard_enter();
	service_running = false;
	local.events_pending = event_count;
	service_guard_leave(flags);
	if (result != NULL)
		*result = local;
	return true;
}

bool
network_service_quiescent(void)
{
	uint64_t flags = service_guard_enter();
	bool result = !service_running && event_count == 0;
	service_guard_leave(flags);
	return result;
}
