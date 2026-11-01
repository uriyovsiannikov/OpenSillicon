#include "kernel/netstack.h"
#include "kernel/tcp_wire.h"
#ifdef OPEN_SILLICON_ENABLE_TCP_ROUTING
#include "kernel/tcp_runtime.h"
#endif

#define ETHERTYPE_IPV4 0x0800U
#define ETHERTYPE_ARP 0x0806U
#define ARP_HARDWARE_ETHERNET 1U
#define ARP_OPERATION_REQUEST 1U
#define ARP_OPERATION_REPLY 2U
#define IP_PROTOCOL_ICMP 1U
#define IP_PROTOCOL_TCP 6U
#define IP_PROTOCOL_UDP 17U

static uint16_t
read_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void
write_be16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static bool
bytes_equal(const uint8_t *a, const uint8_t *b, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (a[i] != b[i])
			return false;
	return true;
}

static bool
is_zero(const uint8_t *p, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (p[i] != 0)
			return false;
	return true;
}

static bool
is_broadcast(const uint8_t *p, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (p[i] != 0xff)
			return false;
	return true;
}

static uint16_t
checksum(const uint8_t *data, size_t length)
{
	uint32_t sum = 0;

	while (length >= 2) {
		sum += read_be16(data);
		data += 2;
		length -= 2;
	}
	if (length != 0)
		sum += (uint16_t)data[0] << 8;
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

bool
netstack_ipv4_classify(const uint8_t *packet, size_t available,
	enum netstack_ipv4_protocol *protocol)
{
	enum netstack_ipv4_protocol classified;
	uint16_t total;
	if (packet == NULL || protocol == NULL ||
	    available < NETSTACK_IPV4_HEADER || (packet[0] >> 4) != 4 ||
	    (packet[0] & 0x0fU) != 5)
		return false;
	total = read_be16(packet + 2);
	if (total < NETSTACK_IPV4_HEADER || total > NETSTACK_MTU ||
	    (size_t)total > available || checksum(packet, NETSTACK_IPV4_HEADER) != 0)
		return false;
	switch (packet[9]) {
	case IP_PROTOCOL_ICMP:
		classified = NETSTACK_IPV4_ICMP;
		break;
	case IP_PROTOCOL_TCP:
		classified = NETSTACK_IPV4_TCP;
		break;
	case IP_PROTOCOL_UDP:
		classified = NETSTACK_IPV4_UDP;
		break;
	default:
		classified = NETSTACK_IPV4_UNSUPPORTED;
		break;
	}
	*protocol = classified;
	return true;
}

static uint16_t
udp_checksum(const uint8_t source[4], const uint8_t destination[4],
	const uint8_t *udp, size_t length)
{
	uint32_t sum = 0;
	for (size_t i = 0; i < 4; i += 2) {
		sum += read_be16(source + i);
		sum += read_be16(destination + i);
	}
	sum += IP_PROTOCOL_UDP;
	sum += (uint16_t)length;
	while (length >= 2) {
		sum += read_be16(udp);
		udp += 2;
		length -= 2;
	}
	if (length != 0)
		sum += (uint16_t)udp[0] << 8;
	while ((sum >> 16) != 0)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

static bool
valid_unicast_ipv4(const uint8_t address[4])
{
	return !is_zero(address, 4) && address[0] < 224 &&
	    !(address[0] == 255 && address[1] == 255 &&
	    address[2] == 255 && address[3] == 255);
}

static bool
on_link(const struct netstack *stack, const uint8_t address[4])
{
	for (size_t i = 0; i < 4; i++)
		if ((address[i] & stack->config.netmask[i]) !=
		    (stack->config.address[i] & stack->config.netmask[i]))
			return false;
	return true;
}

static bool
valid_unicast_mac(const uint8_t mac[6])
{
	return !is_zero(mac, 6) && (mac[0] & 1U) == 0;
}

static bool
valid_netmask(const uint8_t mask[4])
{
	bool saw_zero = false;

	for (size_t i = 0; i < 4; i++) {
		for (unsigned int bit = 0; bit < 8; bit++) {
			bool set = (mask[i] & (uint8_t)(0x80U >> bit)) != 0;
			if (!set)
				saw_zero = true;
			else if (saw_zero)
				return false;
		}
	}
	return !is_zero(mask, 4);
}

bool
netstack_init(struct netstack *stack, const struct netstack_config *config)
{
	if (stack == NULL || config == NULL || !valid_unicast_mac(config->mac) ||
	    !valid_unicast_ipv4(config->address) || !valid_netmask(config->netmask))
		return false;
	*stack = (struct netstack){.config = *config};
	return true;
}

bool
netstack_arp_lookup(struct netstack *stack, const uint8_t address[4],
	uint64_t now, uint8_t mac[6])
{
	if (stack == NULL || address == NULL || mac == NULL)
		return false;
	for (size_t i = 0; i < NETSTACK_ARP_CACHE_SIZE; i++) {
		struct netstack_arp_entry *entry = &stack->arp[i];
		if (!entry->valid)
			continue;
		if (now < entry->updated_at || now - entry->updated_at >=
		    NETSTACK_ARP_TTL_TICKS) {
			entry->valid = false;
			continue;
		}
		if (bytes_equal(entry->address, address, 4)) {
			for (size_t j = 0; j < 6; j++)
				mac[j] = entry->mac[j];
			return true;
		}
	}
	return false;
}

static void
arp_cache_insert(struct netstack *stack, const uint8_t address[4],
	const uint8_t mac[6], uint64_t now)
{
	size_t target = NETSTACK_ARP_CACHE_SIZE;

	for (size_t i = 0; i < NETSTACK_ARP_CACHE_SIZE; i++) {
		struct netstack_arp_entry *entry = &stack->arp[i];
		if (entry->valid && bytes_equal(entry->address, address, 4)) {
			target = i;
			break;
		}
		if (entry->valid && (now < entry->updated_at || now -
		    entry->updated_at >= NETSTACK_ARP_TTL_TICKS)) {
			entry->valid = false;
		}
		if (!entry->valid && target == NETSTACK_ARP_CACHE_SIZE)
			target = i;
	}
	if (target == NETSTACK_ARP_CACHE_SIZE) {
		target = stack->replacement;
		stack->replacement = (stack->replacement + 1) %
		    NETSTACK_ARP_CACHE_SIZE;
	}
	for (size_t i = 0; i < 4; i++)
		stack->arp[target].address[i] = address[i];
	for (size_t i = 0; i < 6; i++)
		stack->arp[target].mac[i] = mac[i];
	stack->arp[target].updated_at = now;
	stack->arp[target].valid = true;
}

static void
ethernet_header(uint8_t *frame, const uint8_t destination[6],
	const uint8_t source[6], uint16_t type)
{
	for (size_t i = 0; i < 6; i++) {
		frame[i] = destination[i];
		frame[6 + i] = source[i];
	}
	write_be16(frame + 12, type);
}

bool
netstack_build_arp_request(const struct netstack *stack,
	const uint8_t target[4], uint8_t *frame, size_t capacity,
	size_t *frame_length)
{
	static const uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};
	uint8_t *arp;
	if (stack == NULL || target == NULL || frame == NULL ||
	    frame_length == NULL || capacity < 60 ||
	    !valid_unicast_ipv4(target) || !on_link(stack, target) ||
	    bytes_equal(target, stack->config.address, 4))
		return false;
	ethernet_header(frame, broadcast, stack->config.mac, ETHERTYPE_ARP);
	arp = frame + NETSTACK_ETHERNET_HEADER;
	write_be16(arp, ARP_HARDWARE_ETHERNET);
	write_be16(arp + 2, ETHERTYPE_IPV4);
	arp[4] = 6;
	arp[5] = 4;
	write_be16(arp + 6, ARP_OPERATION_REQUEST);
	for (size_t i = 0; i < 6; i++)
		arp[8 + i] = stack->config.mac[i];
	for (size_t i = 0; i < 4; i++) {
		arp[14 + i] = stack->config.address[i];
		arp[24 + i] = target[i];
	}
	for (size_t i = 18; i < 24; i++)
		arp[i] = 0;
	for (size_t i = NETSTACK_ETHERNET_HEADER + NETSTACK_ARP_PACKET;
	    i < 60; i++)
		frame[i] = 0;
	*frame_length = 60;
	return true;
}

static bool
build_ipv4_header(uint8_t *ip, size_t payload_length, uint8_t protocol,
	const uint8_t source[4], const uint8_t destination[4])
{
	if (payload_length > NETSTACK_MTU - NETSTACK_IPV4_HEADER)
		return false;
	for (size_t i = 0; i < NETSTACK_IPV4_HEADER; i++)
		ip[i] = 0;
	ip[0] = 0x45;
	write_be16(ip + 2, (uint16_t)(NETSTACK_IPV4_HEADER + payload_length));
	write_be16(ip + 4, 0x153a);
	ip[8] = 64;
	ip[9] = protocol;
	for (size_t i = 0; i < 4; i++) {
		ip[12 + i] = source[i];
		ip[16 + i] = destination[i];
	}
	write_be16(ip + 10, checksum(ip, NETSTACK_IPV4_HEADER));
	return true;
}

bool
netstack_build_echo_request(const struct netstack *stack,
	const uint8_t destination_mac[6], const uint8_t destination[4],
	uint16_t identifier, uint16_t sequence, const uint8_t *payload,
	size_t payload_length, uint8_t *frame, size_t capacity,
	size_t *frame_length)
{
	size_t icmp_length;
	uint8_t *ip;
	uint8_t *icmp;
	if (stack == NULL || destination_mac == NULL || destination == NULL ||
	    frame == NULL || frame_length == NULL ||
	    !valid_unicast_mac(destination_mac) ||
	    !valid_unicast_ipv4(destination) || !on_link(stack, destination) ||
	    (payload_length != 0 && payload == NULL) ||
	    payload_length > NETSTACK_MTU - NETSTACK_IPV4_HEADER - 8)
		return false;
	icmp_length = 8 + payload_length;
	if (capacity < NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + icmp_length)
		return false;
	ethernet_header(frame, destination_mac, stack->config.mac, ETHERTYPE_IPV4);
	ip = frame + NETSTACK_ETHERNET_HEADER;
	if (!build_ipv4_header(ip, icmp_length, IP_PROTOCOL_ICMP,
	    stack->config.address, destination))
		return false;
	icmp = ip + NETSTACK_IPV4_HEADER;
	icmp[0] = 8;
	icmp[1] = 0;
	write_be16(icmp + 2, 0);
	write_be16(icmp + 4, identifier);
	write_be16(icmp + 6, sequence);
	for (size_t i = 0; i < payload_length; i++)
		icmp[8 + i] = payload[i];
	write_be16(icmp + 2, checksum(icmp, icmp_length));
	*frame_length = NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + icmp_length;
	return true;
}

bool
netstack_build_udp_frame(const struct netstack *stack,
	const uint8_t destination_mac[6], const uint8_t destination[4],
	uint16_t source_port, uint16_t destination_port,
	const uint8_t *payload, size_t payload_length,
	uint8_t *frame, size_t capacity, size_t *frame_length)
{
	size_t udp_length;
	uint8_t *ip;
	uint8_t *udp;
	uint16_t value;
	if (stack == NULL || destination_mac == NULL || destination == NULL ||
	    frame == NULL || frame_length == NULL || source_port == 0 ||
	    destination_port == 0 || !valid_unicast_mac(destination_mac) ||
	    !valid_unicast_ipv4(destination) || !on_link(stack, destination) ||
	    (payload_length != 0 && payload == NULL) ||
	    payload_length > NETSTACK_UDP_MAX_PAYLOAD)
		return false;
	udp_length = 8 + payload_length;
	if (capacity < NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + udp_length)
		return false;
	ethernet_header(frame, destination_mac, stack->config.mac, ETHERTYPE_IPV4);
	ip = frame + NETSTACK_ETHERNET_HEADER;
	if (!build_ipv4_header(ip, udp_length, IP_PROTOCOL_UDP,
	    stack->config.address, destination))
		return false;
	udp = ip + NETSTACK_IPV4_HEADER;
	write_be16(udp, source_port);
	write_be16(udp + 2, destination_port);
	write_be16(udp + 4, (uint16_t)udp_length);
	write_be16(udp + 6, 0);
	for (size_t i = 0; i < payload_length; i++)
		udp[8 + i] = payload[i];
	value = udp_checksum(stack->config.address, destination, udp, udp_length);
	write_be16(udp + 6, value == 0 ? 0xffffU : value);
	*frame_length = NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER + udp_length;
	return true;
}

static bool
build_arp_reply(const struct netstack *stack, const uint8_t *request,
	uint8_t *reply, size_t capacity, size_t *reply_length)
{
	const uint8_t *arp = request + NETSTACK_ETHERNET_HEADER;
	uint8_t *out;
	if (capacity < 60)
		return false;
	ethernet_header(reply, arp + 8, stack->config.mac, ETHERTYPE_ARP);
	out = reply + NETSTACK_ETHERNET_HEADER;
	write_be16(out, ARP_HARDWARE_ETHERNET);
	write_be16(out + 2, ETHERTYPE_IPV4);
	out[4] = 6;
	out[5] = 4;
	write_be16(out + 6, ARP_OPERATION_REPLY);
	for (size_t i = 0; i < 6; i++) {
		out[8 + i] = stack->config.mac[i];
		out[18 + i] = arp[8 + i];
	}
	for (size_t i = 0; i < 4; i++) {
		out[14 + i] = stack->config.address[i];
		out[24 + i] = arp[14 + i];
	}
	for (size_t i = NETSTACK_ETHERNET_HEADER + NETSTACK_ARP_PACKET; i < 60; i++)
		reply[i] = 0;
	*reply_length = 60;
	return true;
}

static enum netstack_event
process_arp(struct netstack *stack, const uint8_t *frame, size_t length,
	uint64_t now, uint8_t *reply, size_t capacity, size_t *reply_length)
{
	const uint8_t *arp = frame + NETSTACK_ETHERNET_HEADER;
	uint16_t operation;

	if (length < NETSTACK_ETHERNET_HEADER + NETSTACK_ARP_PACKET ||
	    read_be16(arp) != ARP_HARDWARE_ETHERNET ||
	    read_be16(arp + 2) != ETHERTYPE_IPV4 || arp[4] != 6 || arp[5] != 4)
		return NETSTACK_IGNORED;
	operation = read_be16(arp + 6);
	if ((operation != ARP_OPERATION_REQUEST && operation != ARP_OPERATION_REPLY) ||
	    !valid_unicast_mac(arp + 8) || !valid_unicast_ipv4(arp + 14) ||
	    bytes_equal(arp + 8, stack->config.mac, 6) ||
	    !on_link(stack, arp + 14) || !bytes_equal(frame + 6, arp + 8, 6) ||
	    (operation == ARP_OPERATION_REQUEST &&
	    !is_broadcast(frame, 6) && !bytes_equal(frame, stack->config.mac, 6)) ||
	    (operation == ARP_OPERATION_REQUEST && !is_zero(arp + 18, 6) &&
	    !bytes_equal(arp + 18, stack->config.mac, 6)) ||
	    (operation == ARP_OPERATION_REPLY &&
	    (!bytes_equal(frame, stack->config.mac, 6) ||
	    !bytes_equal(arp + 18, stack->config.mac, 6))) ||
	    !bytes_equal(arp + 24, stack->config.address, 4) ||
	    bytes_equal(arp + 14, stack->config.address, 4))
		return NETSTACK_IGNORED;
	if (operation == ARP_OPERATION_REQUEST &&
	    (reply == NULL || capacity < 60))
		return NETSTACK_IGNORED;
	arp_cache_insert(stack, arp + 14, arp + 8, now);
	if (operation == ARP_OPERATION_REQUEST &&
	    !build_arp_reply(stack, frame, reply, capacity, reply_length))
		return NETSTACK_IGNORED;
	return operation == ARP_OPERATION_REQUEST ? NETSTACK_ARP_REPLY :
	    NETSTACK_ARP_RESOLVED;
}

static enum netstack_event
process_ipv4(struct netstack *stack, const uint8_t *frame, size_t length,
	uint8_t *reply, size_t capacity, size_t *reply_length,
	struct netstack_udp_view *udp_view, struct netstack_icmp_view *icmp_view)
{
	const uint8_t *ip = frame + NETSTACK_ETHERNET_HEADER;
	const uint8_t *icmp;
	enum netstack_ipv4_protocol protocol;
	size_t ip_length;
	size_t icmp_length;
	uint16_t fragment;
	uint16_t total;

	if (length < NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER ||
	    !netstack_ipv4_classify(ip,
	    length - NETSTACK_ETHERNET_HEADER, &protocol))
		return NETSTACK_IGNORED;
	total = read_be16(ip + 2);
	fragment = read_be16(ip + 6);
	if ((fragment & 0xbfffU) != 0 || ip[8] == 0 ||
	    !valid_unicast_ipv4(ip + 12) || !on_link(stack, ip + 12) ||
	    bytes_equal(ip + 12, stack->config.address, 4) ||
	    !valid_unicast_mac(frame + 6) ||
	    !bytes_equal(frame, stack->config.mac, 6) ||
	    !bytes_equal(ip + 16, stack->config.address, 4))
		return NETSTACK_IGNORED;
	ip_length = total;
	if (protocol == NETSTACK_IPV4_UDP) {
		const uint8_t *udp = ip + NETSTACK_IPV4_HEADER;
		size_t udp_length;
		if (ip_length < NETSTACK_IPV4_HEADER + 8)
			return NETSTACK_IGNORED;
		udp_length = read_be16(udp + 4);
		if (udp_length < 8 || udp_length !=
		    ip_length - NETSTACK_IPV4_HEADER ||
		    (read_be16(udp + 6) != 0 &&
		    udp_checksum(ip + 12, ip + 16, udp, udp_length) != 0))
			return NETSTACK_IGNORED;
		if (udp_view == NULL)
			return NETSTACK_IGNORED;
		for (size_t i = 0; i < 4; i++)
			udp_view->source_address[i] = ip[12 + i];
		udp_view->source_port = read_be16(udp);
		udp_view->destination_port = read_be16(udp + 2);
		udp_view->payload = udp + 8;
		udp_view->payload_length = udp_length - 8;
		return udp_view->source_port == 0 || udp_view->destination_port == 0 ?
		    NETSTACK_IGNORED : NETSTACK_UDP_DATAGRAM;
	}
	if (protocol == NETSTACK_IPV4_TCP) {
		const uint8_t *tcp = ip + NETSTACK_IPV4_HEADER;
		size_t tcp_length = ip_length - NETSTACK_IPV4_HEADER;
		struct tcp_wire_pseudo_header pseudo = {0};
		struct tcp_wire_segment parsed;
		enum tcp_wire_error error;
		for (size_t i = 0; i < 4; i++) {
			pseudo.source[i] = ip[12 + i];
			pseudo.destination[i] = ip[16 + i];
		}
		pseudo.protocol = IP_PROTOCOL_TCP;
		pseudo.segment_length = (uint16_t)tcp_length;
		error = tcp_wire_parse(&pseudo, tcp, tcp_length, &parsed);
		switch (error) {
		case TCP_WIRE_OK:
			/* Parsed pointers borrow this frame and are intentionally discarded
			 * before returning to the existing RX owner. */
#ifdef OPEN_SILLICON_ENABLE_TCP_ROUTING
			{
				struct tcp_runtime_route_result route;
				(void)tcp_runtime_route_segment(pseudo.source,
				    pseudo.destination, &parsed, &route);
				if (route.has_reference &&
				    !tcp_runtime_handle_release(route.handle))
					return NETSTACK_TCP_INVALID_HEADER;
			}
#endif
			return NETSTACK_TCP_DISABLED;
		case TCP_WIRE_CHECKSUM:
			return NETSTACK_TCP_INVALID_CHECKSUM;
		case TCP_WIRE_OPTION:
			return NETSTACK_TCP_INVALID_OPTIONS;
		case TCP_WIRE_INVALID_ARGUMENT:
		case TCP_WIRE_LENGTH:
		case TCP_WIRE_HEADER:
		case TCP_WIRE_RESERVED:
		case TCP_WIRE_OUTPUT_SPACE:
		default:
			return NETSTACK_TCP_INVALID_HEADER;
		}
	}
	if (protocol != NETSTACK_IPV4_ICMP)
		return NETSTACK_IGNORED;
	if (ip_length < NETSTACK_IPV4_HEADER + 8)
		return NETSTACK_IGNORED;
	icmp = ip + NETSTACK_IPV4_HEADER;
	icmp_length = ip_length - NETSTACK_IPV4_HEADER;
	if (checksum(icmp, icmp_length) != 0)
		return NETSTACK_IGNORED;
	if (((icmp[0] == 0 || icmp[0] == 8) && icmp[1] != 0) ||
	    (icmp[0] == 3 && icmp[1] > 15))
		return NETSTACK_IGNORED;
	if (icmp[0] == 0) {
		if (icmp_view != NULL) {
			for (size_t i = 0; i < 4; i++)
				icmp_view->source_address[i] = ip[12 + i];
			icmp_view->type = icmp[0];
			icmp_view->code = icmp[1];
			icmp_view->identifier = read_be16(icmp + 4);
			icmp_view->sequence = read_be16(icmp + 6);
			icmp_view->payload = icmp + 8;
			icmp_view->payload_length = icmp_length - 8;
		}
		return NETSTACK_ECHO_REPLY;
	}
	if (icmp[0] == 3) {
		const uint8_t *quoted_ip;
		const uint8_t *quoted_icmp;
		if (icmp_view == NULL || icmp_length < 8 + NETSTACK_IPV4_HEADER + 8)
			return NETSTACK_IGNORED;
		quoted_ip = icmp + 8;
		quoted_icmp = quoted_ip + NETSTACK_IPV4_HEADER;
		if (quoted_ip[0] != 0x45 ||
		    read_be16(quoted_ip + 2) < NETSTACK_IPV4_HEADER + 8 ||
		    quoted_ip[9] != IP_PROTOCOL_ICMP ||
		    quoted_icmp[0] != 8 || quoted_icmp[1] != 0)
			return NETSTACK_IGNORED;
		for (size_t i = 0; i < 4; i++) {
			icmp_view->source_address[i] = ip[12 + i];
			icmp_view->quoted_source[i] = quoted_ip[12 + i];
			icmp_view->quoted_destination[i] = quoted_ip[16 + i];
		}
		icmp_view->type = icmp[0];
		icmp_view->code = icmp[1];
		icmp_view->quoted_type = quoted_icmp[0];
		icmp_view->quoted_identifier = read_be16(quoted_icmp + 4);
		icmp_view->quoted_sequence = read_be16(quoted_icmp + 6);
		return NETSTACK_ICMP_ERROR;
	}
	if (icmp[0] != 8 || reply == NULL ||
	    capacity < NETSTACK_ETHERNET_HEADER + total)
		return NETSTACK_IGNORED;
	ethernet_header(reply, frame + 6, stack->config.mac, ETHERTYPE_IPV4);
	if (!build_ipv4_header(reply + NETSTACK_ETHERNET_HEADER, icmp_length,
	    IP_PROTOCOL_ICMP, stack->config.address, ip + 12))
		return NETSTACK_IGNORED;
	uint8_t *out_icmp = reply + NETSTACK_ETHERNET_HEADER + NETSTACK_IPV4_HEADER;
	for (size_t i = 0; i < icmp_length; i++)
		out_icmp[i] = icmp[i];
	out_icmp[0] = 0;
	write_be16(out_icmp + 2, 0);
	write_be16(out_icmp + 2, checksum(out_icmp, icmp_length));
	*reply_length = NETSTACK_ETHERNET_HEADER + total;
	return NETSTACK_ECHO_REQUEST;
}

enum netstack_event
netstack_process_frame_ex(struct netstack *stack, const uint8_t *frame,
	size_t frame_length, uint64_t now, uint8_t *reply,
	size_t reply_capacity, size_t *reply_length,
	struct netstack_udp_view *udp, struct netstack_icmp_view *icmp)
{
	uint16_t type;
	if (reply_length != NULL)
		*reply_length = 0;
	if (udp != NULL)
		*udp = (struct netstack_udp_view){0};
	if (icmp != NULL)
		*icmp = (struct netstack_icmp_view){0};
	if (stack == NULL || frame == NULL || reply_length == NULL ||
	    frame_length < NETSTACK_ETHERNET_HEADER ||
	    frame_length > NETSTACK_MAX_FRAME)
		return NETSTACK_IGNORED;
	type = read_be16(frame + 12);
	if (type == ETHERTYPE_ARP)
		return process_arp(stack, frame, frame_length, now, reply,
		    reply_capacity, reply_length);
	if (type == ETHERTYPE_IPV4)
		return process_ipv4(stack, frame, frame_length, reply,
		    reply_capacity, reply_length, udp, icmp);
	return NETSTACK_IGNORED;
}

enum netstack_event
netstack_process_frame(struct netstack *stack, const uint8_t *frame,
	size_t frame_length, uint64_t now, uint8_t *reply,
	size_t reply_capacity, size_t *reply_length,
	struct netstack_udp_view *udp)
{
	return netstack_process_frame_ex(stack, frame, frame_length, now, reply,
	    reply_capacity, reply_length, udp, NULL);
}
