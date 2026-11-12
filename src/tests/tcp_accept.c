#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/heap.h"

/* Exercise the real wrapper/FD code. Only the transport queue is mocked, so
 * dequeue and close counts expose whether an error consumes a connection. */
#include "../kernel/net/tcp/tcp_socket.c"

static Socket listener_socket, peers[2];
static unsigned int next_peer, accept_calls, closed[2];
static size_t live_allocations;
static bool fail_allocation;

void *kheap_alloc(size_t size)
{
	if (fail_allocation) { fail_allocation = false; return NULL; }
	void *p = malloc(size);
	if (p != NULL) live_allocations++;
	return p;
}
bool kheap_free(void *p)
{
	if (p != NULL) { assert(live_allocations > 0); live_allocations--; }
	free(p);
	return true;
}
void klog(const char *message) { fputs(message, stderr); abort(); }
_Noreturn void kernel_halt(void) { abort(); }
void process_wake_channel(void *channel) { (void)channel; }
TcpState tcpGetState(Socket *socket)
{
	return socket == &listener_socket ? TCP_STATE_LISTEN : TCP_STATE_ESTABLISHED;
}
Socket *socketAccept(Socket *socket, IpAddr *address, uint16_t *port)
{
	assert(socket == &listener_socket);
	accept_calls++;
	if (next_peer == 2) return NULL;
	address->length = sizeof(Ipv4Addr);
	address->ipv4Addr = IPV4_ADDR(10, 0, 0, 1);
	*port = (uint16_t)(1000 + next_peer);
	return &peers[next_peer++];
}
void socketClose(Socket *socket)
{
	if (socket == &listener_socket) return;
	assert(socket == &peers[0] || socket == &peers[1]);
	closed[socket == &peers[0] ? 0 : 1]++;
}
error_t socketReceive(Socket *socket, void *data, size_t size,
    size_t *received, uint_t flags)
{
	(void)flags;
	assert(size >= 1 && (socket == &peers[0] || socket == &peers[1]));
	*(char *)data = socket == &peers[0] ? 'A' : 'B';
	*received = 1;
	return NO_ERROR;
}
error_t socketSend(Socket *socket, const void *data, size_t size,
    size_t *written, uint_t flags)
{
	(void)socket; (void)data; (void)flags;
	*written = size;
	return NO_ERROR;
}
void socketRegisterEvents(Socket *socket, OsEvent *event, uint_t mask)
{ (void)socket; (void)event; (void)mask; }
uint_t socketGetEvents(Socket *socket) { (void)socket; return 0; }

static int
setup(struct fd_table *table)
{
	memset(table, 0, sizeof(*table));
	memset(tcp_sockets, 0, sizeof(tcp_sockets));
	memset(closed, 0, sizeof(closed));
	tcp_sockets_ready = true;
	next_peer = accept_calls = 0;
	tcp_sockets[0] = (struct tcp_socket_state){.allocated = true, .socket = &listener_socket};
	struct fd_object *object = fd_object_create(&tcp_socket_operations, &tcp_sockets[0]);
	assert(object != NULL);
	int descriptor = fd_install(table, object);
	assert(descriptor >= 0);
	return descriptor;
}

static void
accept_both(struct fd_table *table, int server)
{
	for (unsigned int i = 0; i < 2; i++) {
		struct tcp_socket_endpoint peer;
		char byte;
		size_t amount;
		int descriptor = tcp_socket_accept(table, table->entries[server], &peer);
		assert(descriptor >= 0 && peer.port == 1000 + i && peer.address[0] == 10);
		assert(fd_read(table, (uint64_t)descriptor, &byte, 1, &amount) == 0);
		assert(amount == 1 && byte == 'A' + (char)i);
		assert(fd_close(table, (uint64_t)descriptor) == 0);
		assert(closed[i] == 1);
	}
}

int main(void)
{
	struct fd_table table;
	int server = setup(&table), duplicate;
	while ((duplicate = fd_dup(&table, (uint64_t)server)) >= 0) { }
	assert(duplicate == -24);
	assert(tcp_socket_accept(&table, table.entries[server], NULL) == -24);
	assert(next_peer == 0 && accept_calls == 0 && closed[0] == 0);
	assert(fd_close(&table, 31) == 0);
	accept_both(&table, server);
	fd_table_destroy(&table);
	assert(live_allocations == 0);

	server = setup(&table);
	for (unsigned int i = 0; i < 4; i++) {
		fail_allocation = true;
		assert(tcp_socket_accept(&table, table.entries[server], NULL) == -12);
		assert(!fail_allocation && next_peer == 0 && accept_calls == 0);
		assert(live_allocations == 1 && closed[0] == 0);
	}
	accept_both(&table, server);
	fd_table_destroy(&table);
	assert(live_allocations == 0);

	server = setup(&table);
	for (unsigned int i = 1; i < TCP_SOCKET_LIMIT; i++) tcp_sockets[i].allocated = true;
	assert(tcp_socket_accept(&table, table.entries[server], NULL) == -24);
	assert(next_peer == 0 && accept_calls == 0 && closed[0] == 0);
	tcp_sockets[TCP_SOCKET_LIMIT - 1].allocated = false;
	accept_both(&table, server);
	fd_table_destroy(&table);
	assert(live_allocations == 0);

	server = setup(&table);
	next_peer = 2;
	for (unsigned int i = 0; i < 4; i++) {
		assert(tcp_socket_accept(&table, table.entries[server], NULL) == -11);
		assert(live_allocations == 1 && fd_find_free_descriptor(&table) == 4);
		for (unsigned int j = 1; j < TCP_SOCKET_LIMIT; j++) assert(!tcp_sockets[j].allocated);
	}
	fd_table_destroy(&table);
	assert(live_allocations == 0);
	puts("TCP accept resource tests passed");
	return 0;
}
