#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "abi/syscall.h"
#include "internal.h"

#define DNS_PACKET_MAX 512U
#define DNS_PORT 53U
#define DNS_ANSWER_LIMIT 8U

struct resolver_endpoint {
	uint8_t address[4];
	uint16_t port;
};

_Static_assert(sizeof(struct resolver_endpoint) == 6,
	"UDP endpoint syscall ABI must remain six bytes");

static uint16_t dns_sequence;

static uint16_t
dns_read_u16(const uint8_t *bytes)
{
	return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static void
dns_write_u16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)(value >> 8);
	bytes[1] = (uint8_t)value;
}

static int
parse_ipv4(const char *text, uint8_t address[4])
{
	const char *cursor = text;
	for (size_t part = 0; part < 4; part++) {
		unsigned int value = 0;
		unsigned int digits = 0;
		while (*cursor >= '0' && *cursor <= '9') {
			value = value * 10U + (unsigned int)(*cursor++ - '0');
			if (value > 255U)
				return 0;
			digits++;
		}
		if (digits == 0 || (part != 3 && *cursor++ != '.') ||
		    (part == 3 && *cursor != '\0'))
			return 0;
		address[part] = (uint8_t)value;
	}
	return 1;
}

static int
resolver_ipv4(uint8_t address[4])
{
	char contents[128];
	int descriptor = open("/etc/resolv.conf", O_RDONLY);
	ssize_t length;
	if (descriptor < 0)
		return 0;
	length = read(descriptor, contents, sizeof(contents) - 1U);
	(void)close(descriptor);
	if (length <= 0)
		return 0;
	contents[length] = '\0';
	for (size_t i = 0; i + 10U < (size_t)length; i++) {
		if ((i == 0 || contents[i - 1U] == '\n' ||
		    contents[i - 1U] == ' ' || contents[i - 1U] == '\t') &&
		    memcmp(contents + i, "nameserver", 10) == 0 &&
		    (contents[i + 10U] == ' ' || contents[i + 10U] == '\t')) {
			size_t start = i + 10U;
			size_t end;
			char address_text[16];
			while (contents[start] == ' ' || contents[start] == '\t')
				start++;
			end = start;
			while (end < (size_t)length && contents[end] != ' ' &&
			    contents[end] != '\t' && contents[end] != '\n' &&
			    contents[end] != '\r' && contents[end] != '#')
				end++;
			if (end == start || end - start >= sizeof(address_text))
				return 0;
			memcpy(address_text, contents + start, end - start);
			address_text[end - start] = '\0';
			return parse_ipv4(address_text, address);
		}
	}
	return 0;
}

static int
dns_encode_name(const char *name, uint8_t *packet, size_t capacity,
	size_t *offset)
{
	size_t position = 0;
	if (name == NULL || name[0] == '\0')
		return 0;
	while (name[position] != '\0') {
		size_t start = position;
		while (name[position] != '\0' && name[position] != '.') {
			unsigned char character = (unsigned char)name[position];
			if (character <= 32U || character >= 127U)
				return 0;
			position++;
		}
		if (position == start || position - start > 63U ||
		    *offset + 1U + position - start >= capacity)
			return 0;
		packet[(*offset)++] = (uint8_t)(position - start);
		while (start < position)
			packet[(*offset)++] = (uint8_t)name[start++];
		if (name[position] == '.') {
			position++;
			if (name[position] == '\0')
				break;
		}
	}
	if (*offset >= capacity)
		return 0;
	packet[(*offset)++] = 0;
	return 1;
}

static int
dns_skip_name(const uint8_t *packet, size_t length, size_t *offset)
{
	size_t cursor = *offset;
	for (;;) {
		uint8_t label;
		if (cursor >= length)
			return 0;
		label = packet[cursor++];
		if (label == 0) {
			*offset = cursor;
			return 1;
		}
		if ((label & 0xc0U) == 0xc0U) {
			if (cursor >= length ||
			    ((((size_t)(label & 0x3fU) << 8) | packet[cursor]) >= length))
				return 0;
			*offset = cursor + 1U;
			return 1;
		}
		if ((label & 0xc0U) != 0 || label > 63U ||
		    label > length - cursor)
			return 0;
		cursor += label;
	}
}

static int
dns_lookup_a(const char *name, uint8_t addresses[DNS_ANSWER_LIMIT][4],
	size_t *address_count)
{
	uint8_t query[DNS_PACKET_MAX] = {0};
	uint8_t response[DNS_PACKET_MAX];
	uint8_t encoded_name[256];
	uint8_t server[4];
	struct resolver_endpoint destination = {.port = DNS_PORT};
	struct resolver_endpoint source = {0};
	size_t query_length = 12;
	size_t encoded_length = 0;
	uint16_t identifier = (uint16_t)((uint32_t)getpid() * 257U +
	    ++dns_sequence);
	long descriptor;
	long result;

	*address_count = 0;
	if (!resolver_ipv4(server) ||
	    !dns_encode_name(name, encoded_name, sizeof(encoded_name),
	    &encoded_length))
		return EAI_AGAIN;
	dns_write_u16(query, identifier);
	dns_write_u16(query + 2, 0x0100U);
	dns_write_u16(query + 4, 1);
	if (query_length + encoded_length + 4U > sizeof(query))
		return EAI_NONAME;
	memcpy(query + query_length, encoded_name, encoded_length);
	query_length += encoded_length;
	dns_write_u16(query + query_length, 1);
	dns_write_u16(query + query_length + 2U, 1);
	query_length += 4U;
	memcpy(destination.address, server, sizeof(server));
	descriptor = __libos_call(SYSCALL_UDP_SOCKET, AF_INET, SOCK_DGRAM,
	    IPPROTO_UDP, 0, 0, 0);
	if (descriptor < 0)
		return EAI_AGAIN;
	result = __libos_call(SYSCALL_UDP_SENDTO, descriptor, (long)query,
	    (long)query_length, (long)&destination, 0, 0);
	if (result < 0) {
		(void)close((int)descriptor);
		return EAI_AGAIN;
	}
	result = __libos_call(SYSCALL_UDP_RECVFROM, descriptor, (long)response,
	    sizeof(response), (long)&source, 0, 0);
	(void)close((int)descriptor);
	if (result < 12 || (size_t)result > sizeof(response) ||
	    memcmp(source.address, server, sizeof(server)) != 0 ||
	    source.port != DNS_PORT || dns_read_u16(response) != identifier ||
	    (dns_read_u16(response + 2) & 0xf800U) != 0x8000U ||
	    (dns_read_u16(response + 2) & 0x0200U) != 0 ||
	    dns_read_u16(response + 4) != 1)
		return EAI_AGAIN;
	if ((dns_read_u16(response + 2) & 0x000fU) != 0)
		return (dns_read_u16(response + 2) & 0x000fU) == 3 ?
		    EAI_NONAME : EAI_AGAIN;
	{
		size_t offset = 12;
		if ((size_t)result < offset + encoded_length + 4U ||
		    memcmp(response + offset, encoded_name, encoded_length) != 0)
			return EAI_AGAIN;
		offset += encoded_length;
		if (dns_read_u16(response + offset) != 1 ||
		    dns_read_u16(response + offset + 2U) != 1)
			return EAI_AGAIN;
		offset += 4U;
		for (uint16_t answer = 0, count = dns_read_u16(response + 6);
		    answer < count; answer++) {
			uint16_t type, class_value, data_length;
			if (!dns_skip_name(response, (size_t)result, &offset) ||
			    offset > (size_t)result || (size_t)result - offset < 10U)
				return EAI_AGAIN;
			type = dns_read_u16(response + offset);
			class_value = dns_read_u16(response + offset + 2U);
			data_length = dns_read_u16(response + offset + 8U);
			offset += 10U;
			if (data_length > (size_t)result - offset)
				return EAI_AGAIN;
			if (type == 1 && class_value == 1 && data_length == 4 &&
			    *address_count < DNS_ANSWER_LIMIT) {
				memcpy(addresses[*address_count], response + offset, 4);
				(*address_count)++;
			}
			offset += data_length;
		}
	}
	return *address_count == 0 ? EAI_NONAME : 0;
}

static int
parse_service(const char *service, uint16_t *port)
{
	unsigned int value = 0;
	if (service == NULL) {
		*port = 0;
		return 1;
	}
	if (*service == '\0')
		return 0;
	for (; *service != '\0'; service++) {
		if (*service < '0' || *service > '9')
			return 0;
		value = value * 10U + (unsigned int)(*service - '0');
		if (value > 65535U)
			return 0;
	}
	*port = (uint16_t)value;
	return 1;
}

static struct addrinfo *
make_addrinfo(const uint8_t address[4], uint16_t port, int socktype,
	int protocol, const char *canonname)
{
	struct addrinfo *entry = malloc(sizeof(*entry));
	struct sockaddr_in *socket_address = malloc(sizeof(*socket_address));
	if (entry == NULL || socket_address == NULL) {
		free(entry);
		free(socket_address);
		return NULL;
	}
	memset(entry, 0, sizeof(*entry));
	memset(socket_address, 0, sizeof(*socket_address));
	entry->ai_family = AF_INET;
	entry->ai_socktype = socktype;
	entry->ai_protocol = protocol;
	entry->ai_addrlen = sizeof(*socket_address);
	entry->ai_addr = (struct sockaddr *)socket_address;
	socket_address->sin_family = AF_INET;
	socket_address->sin_port = htons(port);
	socket_address->sin_addr.s_addr = htonl(((uint32_t)address[0] << 24) |
	    ((uint32_t)address[1] << 16) | ((uint32_t)address[2] << 8) |
	    address[3]);
	if (canonname != NULL) {
		size_t length = strlen(canonname) + 1U;
		entry->ai_canonname = malloc(length);
		if (entry->ai_canonname == NULL) {
			free(entry->ai_addr);
			free(entry);
			return NULL;
		}
		memcpy(entry->ai_canonname, canonname, length);
	}
	return entry;
}

int
getaddrinfo(const char *node, const char *service,
	const struct addrinfo *hints, struct addrinfo **result)
{
	uint8_t resolved[DNS_ANSWER_LIMIT][4];
	uint8_t numeric[4];
	size_t resolved_count = 0;
	uint16_t port;
	int family = hints == NULL ? AF_UNSPEC : hints->ai_family;
	int socktype = hints == NULL ? 0 : hints->ai_socktype;
	int protocol = hints == NULL ? 0 : hints->ai_protocol;
	int flags = hints == NULL ? 0 : hints->ai_flags;
	int error;

	if (result == NULL)
		return EAI_FAIL;
	*result = NULL;
	if ((flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
	    AI_NUMERICSERV)) != 0)
		return EAI_BADFLAGS;
	if (family != AF_UNSPEC && family != AF_INET)
		return EAI_FAMILY;
	if ((socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM) ||
	    (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP) ||
	    (socktype == SOCK_STREAM && protocol == IPPROTO_UDP) ||
	    (socktype == SOCK_DGRAM && protocol == IPPROTO_TCP))
		return EAI_SOCKTYPE;
	if (protocol == 0 && socktype == SOCK_STREAM)
		protocol = IPPROTO_TCP;
	else if (protocol == 0 && socktype == SOCK_DGRAM)
		protocol = IPPROTO_UDP;
	if (!parse_service(service, &port))
		return EAI_SERVICE;
	if (node == NULL) {
		uint8_t address[4] = {0, 0, 0, 0};
		struct addrinfo *entry;
		if ((flags & AI_PASSIVE) == 0)
			address[0] = 127, address[3] = 1;
		entry = make_addrinfo(address, port, socktype, protocol,
		    (flags & AI_CANONNAME) != 0 ? "" : NULL);
		if (entry == NULL)
			return EAI_MEMORY;
		*result = entry;
		return 0;
	}
	if (*node == '\0')
		return EAI_NONAME;
	if (parse_ipv4(node, numeric)) {
		memcpy(resolved[0], numeric, sizeof(numeric));
		resolved_count = 1;
	} else {
		if ((flags & AI_NUMERICHOST) != 0)
			return EAI_NONAME;
		error = dns_lookup_a(node, resolved, &resolved_count);
		if (error != 0)
			return error;
	}
	for (size_t i = 0; i < resolved_count; i++) {
		struct addrinfo *entry = make_addrinfo(resolved[i], port, socktype,
		    protocol, (i == 0 && (flags & AI_CANONNAME) != 0) ? node : NULL);
		if (entry == NULL) {
			freeaddrinfo(*result);
			*result = NULL;
			return EAI_MEMORY;
		}
		if (*result == NULL)
			*result = entry;
		else {
			struct addrinfo *tail = *result;
			while (tail->ai_next != NULL)
				tail = tail->ai_next;
			tail->ai_next = entry;
		}
	}
	return 0;
}

void
freeaddrinfo(struct addrinfo *result)
{
	while (result != NULL) {
		struct addrinfo *next = result->ai_next;
		free(result->ai_addr);
		free(result->ai_canonname);
		free(result);
		result = next;
	}
}

const char *
gai_strerror(int error)
{
	switch (error) {
	case 0: return "success";
	case EAI_AGAIN: return "temporary name resolution failure";
	case EAI_BADFLAGS: return "invalid address flags";
	case EAI_FAMILY: return "address family not supported";
	case EAI_MEMORY: return "memory allocation failure";
	case EAI_NONAME: return "name or service not known";
	case EAI_SERVICE: return "service not supported";
	case EAI_SOCKTYPE: return "socket type not supported";
	default: return "address resolution failed";
	}
}

ssize_t
send(int fd, const void *buffer, size_t length, int flags)
{
	if (flags != 0) {
		errno = EOPNOTSUPP;
		return -1;
	}
	return write(fd, buffer, length);
}

ssize_t
recv(int fd, void *buffer, size_t length, int flags)
{
	if (flags != 0) {
		errno = EOPNOTSUPP;
		return -1;
	}
	return read(fd, buffer, length);
}

int
socket(int domain, int type, int protocol)
{
	return (int)__libos_call(SYSCALL_UNIX_SOCKET, domain, type, protocol,
	    0, 0, 0);
}

int
bind(int fd, const struct sockaddr *address, socklen_t length)
{
	return (int)__libos_call(SYSCALL_UNIX_BIND, fd, (long)address, length,
	    0, 0, 0);
}

int
listen(int fd, int backlog)
{
	return (int)__libos_call(SYSCALL_UNIX_LISTEN, fd, backlog, 0, 0, 0, 0);
}

int
connect(int fd, const struct sockaddr *address, socklen_t length)
{
	return (int)__libos_call(SYSCALL_UNIX_CONNECT, fd, (long)address, length,
	    0, 0, 0);
}

int
accept(int fd, struct sockaddr *address, socklen_t *length)
{
	if ((address == 0) != (length == 0)) {
		errno = EINVAL;
		return -1;
	}
	return (int)__libos_call(SYSCALL_UNIX_ACCEPT, fd, (long)address,
	    (long)length, 0, 0, 0);
}

int
shutdown(int fd, int how)
{
	return (int)__libos_call(SYSCALL_UNIX_SHUTDOWN, fd, how, 0, 0, 0, 0);
}

int
getsockopt(int fd, int level, int option, void *value, socklen_t *length)
{
	if (length == 0) {
		errno = EINVAL;
		return -1;
	}
	return (int)__libos_call(SYSCALL_UNIX_GETSOCKOPT, fd, level, option,
	    (long)value, (long)length, 0);
}

int
setsockopt(int fd, int level, int option, const void *value, socklen_t length)
{
	return (int)__libos_call(SYSCALL_UNIX_SETSOCKOPT, fd, level, option,
	    (long)value, length, 0);
}
