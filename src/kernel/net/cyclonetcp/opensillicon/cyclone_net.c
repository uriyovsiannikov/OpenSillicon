#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "kernel/cyclone_net.h"
#include "kernel/heap.h"
#include "kernel/icmp_echo.h"
#include "kernel/netbuf.h"
#include "kernel/network_service.h"
#include "kernel/smp.h"
#include "kernel/timer.h"

#include "core/net.h"
#include "core/net_mem.h"
#include "core/nic.h"
#include "core/udp.h"
#include "core/raw_socket.h"
#include "dhcp/dhcp_client.h"
#include "ipv4/icmp.h"
#include "ipv4/ipv4_misc.h"

#define CYCLONE_RX_BUDGET 8U
#define CYCLONE_TX_POLL_LIMIT 1000000U

const OsTaskParameters OS_TASK_DEFAULT_PARAMS = {0, 0};

static NetContext stack_context;
static NetInterface stack_interface;
static DhcpClientContext dhcp_context;
static struct net_device *ethernet_device;
static bool stack_ready;

struct cyclone_udp_registration {
	uint16_t port;
	cyclone_udp_callback callback;
	void *context;
};
static struct cyclone_udp_registration udp_registrations[UDP_CALLBACK_TABLE_SIZE];

#define CYCLONE_MUTEX_NESTING 32U
struct cyclone_mutex_hold {
	OsMutex *mutex;
	uint64_t interrupt_flags;
};
static struct cyclone_mutex_hold mutex_holds[SMP_MAX_CPUS][CYCLONE_MUTEX_NESTING];
static size_t mutex_hold_depth[SMP_MAX_CPUS];

static size_t
cyclone_cpu_index(void)
{
	struct smp_cpu *cpu = smp_current_cpu();
	return cpu != NULL && cpu->logical_id < SMP_MAX_CPUS ? cpu->logical_id : 0;
}

static uint64_t
cyclone_interrupt_disable(void)
{
	uint64_t flags;
	__asm__ volatile ("pushfq; popq %0; cli" : "=r" (flags) : : "memory");
	return flags;
}

static void
cyclone_interrupt_restore(uint64_t flags)
{
	__asm__ volatile ("pushq %0; popfq" : : "r" (flags) : "memory", "cc");
}

static void
cyclone_udp_dispatch(NetInterface *interface,
	const IpPseudoHeader *pseudo_header, const UdpHeader *header,
	const NetBuffer *buffer, size_t offset,
	const NetRxAncillary *ancillary, void *context)
{
	struct cyclone_udp_registration *registration = context;
	uint8_t source[4];
	uint8_t payload[1472];
	size_t length = ntohs(header->length);
	(void)interface;
	(void)ancillary;
	if (registration == NULL || registration->callback == NULL ||
	    length < sizeof(*header))
		return;
	length -= sizeof(*header);
	if (length > sizeof(payload))
		return;
	for (size_t i = 0; i < 4; i++)
		source[i] = (uint8_t)(pseudo_header->ipv4Data.srcAddr >> (i * 8));
	if (netBufferRead(payload, buffer, offset, length) == length)
		registration->callback(registration->context, source,
		    ntohs(header->srcPort), payload, length);
}

/* All Cyclone entry points are called from the serialized deferred network
 * service. Its no-RTOS task hook is intentionally inert; the kernel invokes
 * netTask once per poll to advance the protocol timers. */
static error_t
cyclone_nic_init(NetInterface *interface)
{
	(void)interface;
	return NO_ERROR;
}

static void
cyclone_nic_noop(NetInterface *interface)
{
	(void)interface;
}

static error_t
cyclone_nic_send(NetInterface *interface, const NetBuffer *buffer,
	size_t offset, NetTxAncillary *ancillary)
{
	struct netbuf_ref packet;
	size_t length = netBufferGetLength(buffer);
	uint8_t *destination;
	int result;
	(void)interface;
	(void)ancillary;

	if (ethernet_device == NULL || length < offset || length - offset >
	    NETBUF_CAPACITY || !netbuf_alloc(NETBUF_OWNER_CORE, &packet))
		return ERROR_OUT_OF_RESOURCES;
	length -= offset;
	destination = netbuf_data(packet, NETBUF_OWNER_CORE);
	if (destination == NULL || netBufferRead(destination, buffer, offset,
	    length) != length || !netbuf_set_length(packet, NETBUF_OWNER_CORE,
	    length)) {
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return ERROR_FAILURE;
	}
	result = net_device_transmit(ethernet_device, packet);
	if (result != NET_DEVICE_OK) {
		(void)netbuf_release(packet, NETBUF_OWNER_CORE);
		return ERROR_FAILURE;
	}
	if (net_device_wait_transmit(ethernet_device, packet,
	    CYCLONE_TX_POLL_LIMIT) != NET_DEVICE_OK) {
		/* A timed-out TX may still be owned by the device. Do not free it. */
		return ERROR_FAILURE;
	}
	return netbuf_release(packet, NETBUF_OWNER_CORE) ? NO_ERROR : ERROR_FAILURE;
}

static const NicDriver cyclone_nic_driver = {
	.type = NIC_TYPE_ETHERNET,
	.mtu = 1500,
	.init = cyclone_nic_init,
	.tick = cyclone_nic_noop,
	.enableIrq = cyclone_nic_noop,
	.disableIrq = cyclone_nic_noop,
	.eventHandler = cyclone_nic_noop,
	.sendPacket = cyclone_nic_send,
	.autoPadding = FALSE,
	.autoCrcCalc = FALSE,
	.autoCrcVerif = FALSE,
	.autoCrcStrip = TRUE
};

bool
cyclone_net_init(struct net_device *device)
{
	NetSettings settings = {0};
	DhcpClientSettings dhcp_settings;
	MacAddr mac;

	if (stack_ready || device == NULL || device->state != NET_DEVICE_UP)
		return false;
	settings.interfaces = &stack_interface;
	settings.numInterfaces = 1;
	if (netInit(&stack_context, &settings) != NO_ERROR)
		return false;
	stack_interface.nicDriver = &cyclone_nic_driver;
	for (size_t i = 0; i < 6; i++)
		mac.b[i] = device->mac_address[i];
	if (netSetMacAddr(&stack_interface, &mac) != NO_ERROR ||
	    netConfigInterface(&stack_interface) != NO_ERROR ||
	    ipv4SetHostAddr(&stack_interface, IPV4_UNSPECIFIED_ADDR) != NO_ERROR ||
	    ipv4SetSubnetMask(&stack_interface, IPV4_UNSPECIFIED_ADDR) != NO_ERROR ||
	    ipv4SetDefaultGateway(&stack_interface, IPV4_UNSPECIFIED_ADDR) != NO_ERROR ||
	    netStartInterface(&stack_interface) != NO_ERROR ||
	    netStart(&stack_context) != NO_ERROR)
		return false;
	dhcpClientGetDefaultSettings(&dhcp_settings);
	dhcp_settings.interface = &stack_interface;
	if (dhcpClientInit(&dhcp_context, &dhcp_settings) != NO_ERROR ||
	    dhcpClientStart(&dhcp_context) != NO_ERROR)
		return false;
	osSetEvent(&stack_interface.nicTxEvent);
	ethernet_device = device;
	stack_ready = true;
	return true;
}

size_t
cyclone_net_poll(size_t budget)
{
	struct netbuf_ref packets[CYCLONE_RX_BUDGET];
	size_t count = 0;
	size_t processed = 0;
	bool link_up;
	if (!stack_ready || budget == 0)
		return 0;
	link_up = net_device_link_up(ethernet_device);
	(void)netSetLinkState(&stack_interface, link_up ? TRUE : FALSE);
	if (budget > CYCLONE_RX_BUDGET)
		budget = CYCLONE_RX_BUDGET;
	/* Drain already received frames even while the PHY reports link down.
	 * In particular, DHCP replies may be queued during a link transition. */
	if (net_device_receive(ethernet_device, budget, packets, budget,
	    &count) == NET_DEVICE_OK) {
		if (count != 0)
			netLock(&stack_context);
		for (size_t i = 0; i < count; i++) {
			uint8_t *frame = netbuf_data(packets[i], NETBUF_OWNER_CORE);
			size_t length = netbuf_length(packets[i], NETBUF_OWNER_CORE);
			if (frame != NULL) {
				NetRxAncillary ancillary = {0};
				nicProcessPacket(&stack_interface, frame, length,
				    &ancillary);
				processed++;
			}
			(void)netbuf_release(packets[i], NETBUF_OWNER_CORE);
		}
		if (count != 0)
			netUnlock(&stack_context);
	}
	/* Run timers and link/DHCP state transitions even while the cable is
	 * disconnected. NET_RTOS_SUPPORT=DISABLED makes this a bounded pass. */
	netTask(&stack_context);
	return processed;
}

bool
cyclone_net_ready(void)
{
	return stack_ready;
}

bool
cyclone_net_ipv4_config(uint8_t address[4], uint8_t netmask[4],
	uint8_t gateway[4])
{
	Ipv4AddrEntry *entry;
	Ipv4Addr host_address;
	if (!stack_ready || address == NULL || netmask == NULL || gateway == NULL)
		return false;
	netLock(&stack_context);
	entry = &stack_interface.ipv4Context.addrList[0];
	if (entry->state != IPV4_ADDR_STATE_VALID) {
		netUnlock(&stack_context);
		return false;
	}
	host_address = entry->addr;
	for (size_t i = 0; i < 4; i++) {
		address[i] = (uint8_t)(host_address >> (i * 8));
		netmask[i] = (uint8_t)(entry->subnetMask >> (i * 8));
		gateway[i] = (uint8_t)(entry->defaultGateway >> (i * 8));
	}
	netUnlock(&stack_context);
	return host_address != IPV4_UNSPECIFIED_ADDR;
}

bool
cyclone_net_dns_server(uint8_t address[4])
{
	Ipv4Addr server;
	if (!stack_ready || address == NULL ||
	    ipv4GetDnsServer(&stack_interface, 0, &server) != NO_ERROR ||
	    server == IPV4_UNSPECIFIED_ADDR)
		return false;
	for (size_t i = 0; i < 4; i++)
		address[i] = (uint8_t)(server >> (i * 8));
	return true;
}

bool
cyclone_net_udp_register(uint16_t port, cyclone_udp_callback callback, void *context)
{
	if (!stack_ready || port == 0 || callback == NULL)
		return false;
	netLock(&stack_context);
	for (size_t i = 0; i < UDP_CALLBACK_TABLE_SIZE; i++) {
		if (udp_registrations[i].callback == NULL) {
			udp_registrations[i] = (struct cyclone_udp_registration){
				.port = port, .callback = callback, .context = context
			};
			if (udpRegisterRxCallback(&stack_interface, port,
			    cyclone_udp_dispatch, &udp_registrations[i]) == NO_ERROR) {
				netUnlock(&stack_context);
				return true;
			}
			udp_registrations[i] = (struct cyclone_udp_registration){0};
			netUnlock(&stack_context);
			return false;
		}
	}
	netUnlock(&stack_context);
	return false;
}

bool
cyclone_net_udp_unregister(uint16_t port)
{
	if (!stack_ready || port == 0)
		return false;
	netLock(&stack_context);
	if (udpUnregisterRxCallback(&stack_interface, port) != NO_ERROR) {
		netUnlock(&stack_context);
		return false;
	}
	for (size_t i = 0; i < UDP_CALLBACK_TABLE_SIZE; i++) {
		if (udp_registrations[i].port == port)
			udp_registrations[i] = (struct cyclone_udp_registration){0};
	}
	netUnlock(&stack_context);
	return true;
}

int
cyclone_net_udp_send(uint16_t source_port, const uint8_t destination[4],
	uint16_t destination_port, const uint8_t *payload, size_t length)
{
	IpAddr source = { .length = sizeof(Ipv4Addr) };
	IpAddr target = { .length = sizeof(Ipv4Addr) };
	NetBuffer *buffer;
	size_t offset;
	error_t error;
	if (!stack_ready || destination == NULL || destination_port == 0 ||
	    (length != 0 && payload == NULL) || length > 1472)
		return -1;
	target.ipv4Addr = IPV4_ADDR(destination[0], destination[1],
	    destination[2], destination[3]);
	buffer = udpAllocBuffer(length, &offset);
	if (buffer == NULL)
		return -1;
	if (netBufferWrite(buffer, offset, payload, length) != length) {
		netBufferFree(buffer);
		return -1;
	}
	netLock(&stack_context);
	source.ipv4Addr = stack_interface.ipv4Context.addrList[0].addr;
	error = source.ipv4Addr == IPV4_UNSPECIFIED_ADDR ? ERROR_INVALID_ADDRESS :
	    udpSendBuffer(&stack_context, &stack_interface, &source, source_port,
	    &target, destination_port, buffer, offset, NULL);
	netUnlock(&stack_context);
	netBufferFree(buffer);
	return error == NO_ERROR ? 0 : -1;
}

bool
cyclone_net_can_route(const uint8_t destination[4])
{
	NetInterface *interface = &stack_interface;
	Ipv4Addr source;
	Ipv4AddrEntry *entry;
	Ipv4Addr target;
	bool result;
	if (!stack_ready || destination == NULL)
		return false;
	target = IPV4_ADDR(destination[0], destination[1], destination[2],
	    destination[3]);
	netLock(&stack_context);
	entry = &stack_interface.ipv4Context.addrList[0];
	result = entry->state == IPV4_ADDR_STATE_VALID &&
	    ipv4SelectSourceAddr(&stack_context, &interface, target, &source) ==
	    NO_ERROR && ((target & entry->subnetMask) ==
	    (entry->addr & entry->subnetMask) ||
	    entry->defaultGateway != IPV4_UNSPECIFIED_ADDR);
	netUnlock(&stack_context);
	return result;
}

bool
cyclone_net_is_local(const uint8_t address[4])
{
	Ipv4Addr local;
	if (!stack_ready || address == NULL ||
	    ipv4GetHostAddr(&stack_interface, &local) != NO_ERROR)
		return false;
	return local == IPV4_ADDR(address[0], address[1], address[2], address[3]);
}

int
cyclone_net_icmp_echo(const uint8_t destination[4], uint16_t identifier,
	uint16_t sequence, const uint8_t *payload, size_t length,
	uint8_t reply_source[4], uint64_t *elapsed_ticks, uint64_t timeout_ticks)
{
	Socket *socket;
	IpAddr target = { .length = sizeof(Ipv4Addr) };
	IcmpEchoMessage request;
	uint8_t packet[sizeof(IcmpEchoMessage) + ICMP_ECHO_PAYLOAD_LIMIT];
	uint64_t start;
	size_t written = 0;
	error_t error;
	if (!cyclone_net_can_route(destination) || reply_source == NULL ||
	    elapsed_ticks == NULL || length > ICMP_ECHO_PAYLOAD_LIMIT ||
	    (length != 0 && payload == NULL))
		return -1;
	target.ipv4Addr = IPV4_ADDR(destination[0], destination[1],
	    destination[2], destination[3]);
	request.type = ICMP_TYPE_ECHO_REQUEST;
	request.code = 0;
	request.checksum = 0;
	request.identifier = htons(identifier);
	request.sequenceNumber = htons(sequence);
	memcpy(packet, &request, sizeof(request));
	if (length != 0)
		memcpy(packet + sizeof(request), payload, length);
	((IcmpEchoMessage *)packet)->checksum = ipCalcChecksum(packet,
	    sizeof(request) + length);
	socket = socketOpenEx(&stack_context, SOCKET_TYPE_RAW_IP,
	    SOCKET_IP_PROTO_ICMP);
	if (socket == NULL)
		return -1;
	error = socketSendTo(socket, &target, 0, packet,
	    sizeof(request) + length, &written, 0);
	if (error != NO_ERROR || written != sizeof(request) + length) {
		socketClose(socket);
		return -1;
	}
	start = timer_ticks();
	for (;;) {
		SocketMsg message = SOCKET_DEFAULT_MSG;
		uint8_t response[sizeof(IcmpEchoMessage) + ICMP_ECHO_PAYLOAD_LIMIT];
		message.data = response;
		message.size = sizeof(response);
		error = socketReceiveMsg(socket, &message, SOCKET_FLAG_DONT_WAIT);
		if (error == NO_ERROR && message.length >= sizeof(IcmpEchoMessage)) {
			IcmpEchoMessage *echo = (IcmpEchoMessage *)response;
			if (message.srcIpAddr.length == sizeof(Ipv4Addr) &&
			    message.srcIpAddr.ipv4Addr == target.ipv4Addr &&
			    echo->type == ICMP_TYPE_ECHO_REPLY && echo->code == 0 &&
			    ntohs(echo->identifier) == identifier &&
			    ntohs(echo->sequenceNumber) == sequence &&
			    message.length == sizeof(*echo) + length &&
			    ipCalcChecksum(response, message.length) == 0 &&
			    (length == 0 || memcmp(echo->data, payload, length) == 0)) {
				for (size_t i = 0; i < 4; i++)
					reply_source[i] = (uint8_t)(message.srcIpAddr.ipv4Addr >>
					    (i * 8));
				*elapsed_ticks = timer_ticks() - start;
				socketClose(socket);
				return 0;
			}
		} else if (error != ERROR_WOULD_BLOCK && error != ERROR_TIMEOUT) {
			socketClose(socket);
			return -1;
		}
		if (timer_ticks() - start >= timeout_ticks) {
			socketClose(socket);
			return -2;
		}
		timer_wait_for_interrupt();
		/* The ping syscall owns this wait loop, so no other syscall safe
		 * point is guaranteed to poll RX while the reply is in flight. */
		(void)network_service_request(NETWORK_SERVICE_POLL_RX);
		(void)network_service_run(NETWORK_SERVICE_MAX_EVENTS, NULL);
	}
}

/* CycloneCommon no-RTOS adapter: networking runs synchronously in the
 * deferred service, so task creation is only an acknowledgement. */
volatile systime_t systemTicks;

void osInitKernel(void)
{
	systemTicks = (systime_t)(timer_monotonic_ns() / UINT64_C(1000000));
}
void osStartKernel(void) { }
OsTaskId osCreateTask(const char_t *name, OsTaskCode code, void *arg,
	const OsTaskParameters *params)
{
	(void)name; (void)code; (void)arg; (void)params;
	return 1;
}
void osDeleteTask(OsTaskId taskId) { (void)taskId; }
void osDelayTask(systime_t delay) { (void)delay; }
void osSwitchTask(void) { }
void osSuspendAllTasks(void) { }
void osResumeAllTasks(void) { }
bool_t osCreateEvent(OsEvent *event) { if (!event) return FALSE; *event = FALSE; return TRUE; }
void osDeleteEvent(OsEvent *event) { if (event) *event = FALSE; }
void osSetEvent(OsEvent *event) { if (event) *event = TRUE; }
void osResetEvent(OsEvent *event) { if (event) *event = FALSE; }
bool_t osWaitForEvent(OsEvent *event, systime_t timeout)
{
	(void)timeout;
	if (event == NULL || !*event) return FALSE;
	*event = FALSE;
	return TRUE;
}
bool_t osSetEventFromIsr(OsEvent *event) { if (!event) return FALSE; *event = TRUE; return TRUE; }
bool_t osCreateSemaphore(OsSemaphore *semaphore, uint_t count) { if (!semaphore) return FALSE; *semaphore = count; return TRUE; }
void osDeleteSemaphore(OsSemaphore *semaphore) { if (semaphore) *semaphore = 0; }
bool_t osWaitForSemaphore(OsSemaphore *semaphore, systime_t timeout)
{
	(void)timeout;
	if (semaphore == NULL || *semaphore == 0) return FALSE;
	--*semaphore;
	return TRUE;
}
void osReleaseSemaphore(OsSemaphore *semaphore) { if (semaphore) ++*semaphore; }
bool_t osCreateMutex(OsMutex *mutex)
{
	if (mutex == NULL)
		return FALSE;
	__atomic_store_n(mutex, 0, __ATOMIC_RELAXED);
	return TRUE;
}
void osDeleteMutex(OsMutex *mutex)
{
	if (mutex != NULL)
		__atomic_store_n(mutex, 0, __ATOMIC_RELAXED);
}
void osAcquireMutex(OsMutex *mutex)
{
	uint64_t flags;
	size_t cpu, depth;
	if (mutex == NULL)
		return;
	/* Prevent a same-CPU task switch while spinning or holding the lock.
	 * Other CPUs still serialize through the atomic mutex word. */
	flags = cyclone_interrupt_disable();
	while (__atomic_exchange_n(mutex, 1, __ATOMIC_ACQUIRE) != 0) {
		while (__atomic_load_n(mutex, __ATOMIC_RELAXED) != 0)
			__asm__ volatile ("pause" : : : "memory");
	}
	cpu = cyclone_cpu_index();
	depth = mutex_hold_depth[cpu];
	if (depth < CYCLONE_MUTEX_NESTING) {
		mutex_holds[cpu][depth] = (struct cyclone_mutex_hold){mutex, flags};
		mutex_hold_depth[cpu] = depth + 1;
	}
}
void osReleaseMutex(OsMutex *mutex)
{
	size_t cpu, depth, index;
	uint64_t flags;
	if (mutex == NULL)
		return;
	cpu = cyclone_cpu_index();
	depth = mutex_hold_depth[cpu];
	for (index = depth; index > 0; index--)
		if (mutex_holds[cpu][index - 1].mutex == mutex)
			break;
	if (index == 0)
		return;
	index--;
	flags = mutex_holds[cpu][index].interrupt_flags;
	if (index + 1 < depth)
		mutex_holds[cpu][index + 1].interrupt_flags = flags;
	for (size_t i = index; i + 1 < depth; i++)
		mutex_holds[cpu][i] = mutex_holds[cpu][i + 1];
	mutex_holds[cpu][depth - 1] = (struct cyclone_mutex_hold){0};
	mutex_hold_depth[cpu] = depth - 1;
	__atomic_store_n(mutex, 0, __ATOMIC_RELEASE);
	if (mutex_hold_depth[cpu] == 0)
		cyclone_interrupt_restore(flags);
}
systime_t osGetSystemTime(void)
{
	return (systime_t)(timer_monotonic_ns() / UINT64_C(1000000));
}
void *osAllocMem(size_t size) { return kheap_alloc(size); }
void osFreeMem(void *p) { kheap_free(p); }
