#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "kernel/fd.h"
#include "kernel/netbuf.h"
#include "kernel/netdev.h"
#include "kernel/netstack.h"
#include "kernel/network_service.h"
#include "kernel/tcp_wire.h"
#include "kernel/udp_socket.h"

struct fake_rx {
	unsigned int next;
	bool try_reentrant_poll;
	int reentrant_poll_result;
	bool fail_next_receive;
	bool try_shutdown_during_poll;
	bool shutdown_refused_during_poll;
	struct netbuf_ref tcp_references[7];
};

static const struct netstack_config config = {
	.mac = {0x52, 0x54, 0x00, 0x15, 0x03, 0x01},
	.address = {10, 15, 3, 1},
	.netmask = {255, 255, 255, 0}
};

static uint16_t
sum(const uint8_t *bytes, size_t length)
{
	uint32_t value = 0;
	while (length >= 2) {
		value += ((uint16_t)bytes[0] << 8) | bytes[1];
		bytes += 2;
		length -= 2;
	}
	if (length != 0)
		value += (uint16_t)bytes[0] << 8;
	while (value >> 16)
		value = (value & 0xffffU) + (value >> 16);
	return (uint16_t)~value;
}

static void
be16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)(value >> 8);
	bytes[1] = (uint8_t)value;
}

static void
make_datagram(uint8_t *frame, uint16_t destination_port, uint8_t marker)
{
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	static const uint8_t peer_ip[4] = {10, 15, 3, 2};
	uint8_t *ip = frame + 14;
	uint8_t *udp = ip + 20;
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	be16(frame + 12, 0x0800);
	for (size_t i = 0; i < 20; i++) ip[i] = 0;
	ip[0] = 0x45;
	be16(ip + 2, 30);
	ip[8] = 64;
	ip[9] = 17;
	for (size_t i = 0; i < 4; i++) {
		ip[12 + i] = peer_ip[i];
		ip[16 + i] = config.address[i];
	}
	be16(ip + 10, sum(ip, 20));
	be16(udp, 6100);
	be16(udp + 2, destination_port);
	be16(udp + 4, 10);
	be16(udp + 6, 0); /* IPv4 permits an omitted UDP checksum. */
	udp[8] = marker;
	udp[9] = (uint8_t)~marker;
}

static size_t
make_tcp_frame(uint8_t *frame, unsigned int kind)
{
	static const uint8_t peer_mac[6] = {0x52, 0x54, 0, 0x15, 3, 2};
	static const uint8_t peer_ip[4] = {10, 15, 3, 2};
	uint8_t *ip = frame + 14;
	uint8_t *tcp = ip + 20;
	bool has_options = kind == 2 || kind == 5;
	size_t tcp_length = kind == 3 ? 19 : has_options ? 24 : 20;
	uint16_t tcp_sum;
	for (size_t i = 0; i < 6; i++) {
		frame[i] = config.mac[i];
		frame[6 + i] = peer_mac[i];
	}
	be16(frame + 12, 0x0800);
	for (size_t i = 0; i < 20; i++) ip[i] = 0;
	ip[0] = 0x45;
	be16(ip + 2, (uint16_t)(20 + tcp_length));
	ip[8] = 64;
	ip[9] = kind == 6 ? 99 : 6;
	for (size_t i = 0; i < 4; i++) {
		ip[12 + i] = peer_ip[i];
		ip[16 + i] = config.address[i];
	}
	be16(ip + 10, sum(ip, 20));
	for (size_t i = 0; i < tcp_length; i++) tcp[i] = 0;
	be16(tcp, 443);
	be16(tcp + 2, 5555);
	tcp[12] = has_options ? 0x60 : 0x50;
	tcp[13] = 0x02;
	be16(tcp + 14, 4096);
	if (kind == 2) {
		/* MSS has a length of 3 instead of its required length of 4. */
		tcp[20] = 2;
		tcp[21] = 3;
		tcp[22] = 5;
		tcp[23] = 0;
	} else if (kind == 5) {
		tcp[20] = 30; /* unknown but length-delimited option */
		tcp[21] = 4;
		tcp[22] = 0xaa;
		tcp[23] = 0xbb;
	}
	if (tcp_length >= TCP_WIRE_HEADER_MIN) {
		assert(tcp_wire_checksum(peer_ip, config.address, tcp, tcp_length,
		    &tcp_sum) == TCP_WIRE_OK);
		be16(tcp + 16, tcp_sum);
	}
	if (kind == 4)
		ip[10] ^= 1; /* Invalid outer IPv4 checksum. */
	else
		be16(ip + 10, sum(ip, 20));
	return 14 + 20 + tcp_length;
}

static int
fake_receive(struct net_device *device, size_t budget, struct netbuf_ref *packets,
	size_t capacity, size_t *count)
{
	struct fake_rx *rx = device->driver_context;
	struct netbuf_ref reference;
	uint8_t *data;
	(void)budget;
	if (rx->fail_next_receive) {
		rx->fail_next_receive = false;
		*count = 0;
		return NET_DEVICE_IO_ERROR;
	}
	if (rx->try_reentrant_poll) {
		rx->try_reentrant_poll = false;
		rx->reentrant_poll_result = udp_socket_poll();
	}
	if (rx->try_shutdown_during_poll) {
		rx->try_shutdown_during_poll = false;
		rx->shutdown_refused_during_poll = !udp_socket_system_shutdown();
	}
	if (capacity == 0 || rx->next == 13) {
		*count = 0;
		return NET_DEVICE_OK;
	}
	if (!netbuf_alloc(NETBUF_OWNER_DEVICE, &reference))
		return NET_DEVICE_NO_BUFFER;
	data = netbuf_data(reference, NETBUF_OWNER_DEVICE);
	if (rx->next < 7) {
		size_t tcp_frame_length = make_tcp_frame(data, rx->next);
		rx->tcp_references[rx->next] = reference;
		if (rx->next == 1)
			data[14 + 20 + 16] ^= 1; /* Invalid TCP checksum. */
		if (rx->next == 6) {
			uint8_t *ip = data + 14;
			be16(ip + 10, 0);
			be16(ip + 10, sum(ip, 20));
		}
		assert(netbuf_set_length(reference, NETBUF_OWNER_DEVICE,
		    tcp_frame_length));
	} else if (rx->next == 7) {
		make_datagram(data, 9999, 0x10); /* unmatched endpoint */
	} else {
		make_datagram(data, 5555, (uint8_t)(0x20 + rx->next - 8));
	}
	if (rx->next >= 7)
		assert(netbuf_set_length(reference, NETBUF_OWNER_DEVICE, 44));
	assert(netbuf_transfer(reference, NETBUF_OWNER_DEVICE, NETBUF_OWNER_CORE));
	packets[0] = reference;
	*count = 1;
	rx->next++;
	return NET_DEVICE_OK;
}

static void
fake_shutdown(struct net_device *device)
{
	(void)device;
}

static bool
fake_link(const struct net_device *device)
{
	(void)device;
	return true;
}

static const struct net_device_ops fake_ops = {
	.shutdown = fake_shutdown,
	.link_up = fake_link,
	.receive = fake_receive
};

void *kheap_alloc(size_t size) { return malloc(size); }
bool kheap_free(void *pointer) { free(pointer); return true; }
bool tty_write(const void *buffer, size_t length)
{ (void)buffer; (void)length; return true; }
bool serial_write(const void *buffer, size_t length)
{ (void)buffer; (void)length; return true; }
bool tty_read(void *buffer, size_t length, size_t *read_length)
{ (void)buffer; (void)length; *read_length = 0; return true; }
void klog(const char *message) { (void)message; }
uint64_t timer_ticks(void) { return 1; }
_Noreturn void kernel_halt(void) { abort(); }

int
main(void)
{
	struct fd_table table, forked;
	struct fake_rx rx = {0};
	struct net_device device = {
		.name = "fakeudp0", .operations = &fake_ops,
		.driver_context = &rx, .mtu = 1500, .state = NET_DEVICE_UP
	};
	struct udp_socket_endpoint local = {.address = {10, 15, 3, 1}, .port = 5555};
	struct udp_socket_endpoint wildcard = {.port = 5556};
	struct udp_socket_endpoint foreign = {.address = {10, 15, 3, 99}, .port = 5557};
	struct udp_socket_endpoint ephemeral = {0}, assigned;
	struct udp_socket_datagram datagram;
	int descriptors[UDP_SOCKET_LIMIT];

	assert(netbuf_pool_init());
	assert(network_service_init());
	fd_table_init_standard(&table);
	assert(udp_socket_system_init(&device, &config) == 0);
	for (size_t i = 0; i < UDP_SOCKET_LIMIT; i++) {
		descriptors[i] = udp_socket_create_fd(&table, 2, 2, 17);
		assert(descriptors[i] >= 0);
	}
	assert(udp_socket_create_fd(&table, 2, 2, 17) == -24);
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[0]),
	    &local) == 0);
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[1]),
	    &local) == -98);
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[2]),
	    &wildcard) == 0);
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[3]),
	    &(struct udp_socket_endpoint){.address = {10, 15, 3, 1}, .port = 5556})
	    == -98);
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[4]),
	    &foreign) == -99);
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[4]),
	    &ephemeral) == 0);
	assert(udp_socket_local_endpoint(fd_object_for_descriptor(&table,
	    descriptors[4]), &assigned) == 0);
	assert(assigned.port >= UDP_SOCKET_EPHEMERAL_FIRST &&
	    assigned.port <= UDP_SOCKET_EPHEMERAL_LAST);

	/* FD-table cloning shares the socket object until its final reference. */
	assert(fd_table_clone(&forked, &table));
	assert(fd_close(&table, descriptors[0]) == 0);
	assert(udp_socket_is_object(fd_object_for_descriptor(&forked,
	    descriptors[0])));
	assert(fd_close(&forked, descriptors[0]) == 0);
	assert(!udp_socket_is_object(fd_object_for_descriptor(&forked,
	    descriptors[0])));
	assert(udp_socket_bind(fd_object_for_descriptor(&table, descriptors[1]),
	    &local) == 0);

	/* The receive owner rejects recursive polling from a driver callback. */
	rx.fail_next_receive = true;
	assert(udp_socket_poll() == -5); /* -EIO */
	rx.try_reentrant_poll = true;
	{
		size_t available = netbuf_pool_available();
	assert(udp_socket_poll() == 1);
	assert(rx.reentrant_poll_result == -11);
	assert(netbuf_data(rx.tcp_references[0], NETBUF_OWNER_CORE) == NULL);
	assert(netbuf_pool_available() == available);
	}
	/* Valid, malformed, and unsupported TCP/IP frames are parsed or rejected,
	 * then released by the same single RX owner without entering UDP queues. */
	assert(udp_socket_peek(fd_object_for_descriptor(&table, descriptors[1]),
	    &datagram) == -11);
	assert(network_service_quiescent());
	{
		size_t available = netbuf_pool_available();
		for (size_t i = 1; i < 7; i++) {
			assert(udp_socket_poll() == 1);
			assert(netbuf_data(rx.tcp_references[i],
			    NETBUF_OWNER_CORE) == NULL);
		}
		assert(netbuf_pool_available() == available);
	}
	assert(network_service_quiescent());
	assert(udp_socket_peek(fd_object_for_descriptor(&table, descriptors[1]),
	    &datagram) == -11);

	/* One unmatched packet is dropped; queue stores four and drops newest. */
	for (size_t i = 0; i < 6; i++)
		assert(udp_socket_poll() >= 0);
	assert(udp_socket_peek(fd_object_for_descriptor(&table, descriptors[1]),
	    &datagram) == 0);
	assert(datagram.length == 2 && datagram.payload[0] == 0x20);
	for (uint8_t marker = 0x20; marker <= 0x23; marker++) {
		assert(udp_socket_peek(fd_object_for_descriptor(&table, descriptors[1]),
		    &datagram) == 0 && datagram.payload[0] == marker);
		assert(udp_socket_consume(fd_object_for_descriptor(&table,
		    descriptors[1])) == 0);
	}
	assert(udp_socket_peek(fd_object_for_descriptor(&table, descriptors[1]),
	    &datagram) == -11);

	fd_table_destroy(&forked);
	fd_table_destroy(&table);
	assert(network_service_request(NETWORK_SERVICE_POLL_RX));
	assert(!udp_socket_system_shutdown());
	rx.try_shutdown_during_poll = true;
	{
		struct network_service_result result;
		assert(network_service_run(1, &result));
		assert(result.events_processed == 1 && result.events_pending == 0);
		assert(result.errors == 0 && result.frames_processed == 0);
	}
	assert(rx.shutdown_refused_during_poll && device.state == NET_DEVICE_UP);
	assert(udp_socket_system_shutdown());
	assert(device.state == NET_DEVICE_DOWN);
	puts("UDP socket endpoint, queue, FD lifetime, single RX owner, and shutdown tests passed");
	return 0;
}
