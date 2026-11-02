#ifndef LIBOS_NETINET_IN_H
#define LIBOS_NETINET_IN_H

#include <sys/socket.h>
#include <stdint.h>

typedef unsigned int in_addr_t;
typedef unsigned short in_port_t;
struct in_addr { in_addr_t s_addr; };
struct sockaddr_in {
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	unsigned char sin_zero[8];
};
struct in6_addr { unsigned char s6_addr[16]; };
struct sockaddr_in6 {
	sa_family_t sin6_family;
	in_port_t sin6_port;
	unsigned int sin6_flowinfo;
	struct in6_addr sin6_addr;
	unsigned int sin6_scope_id;
};
#define INADDR_ANY ((in_addr_t)0)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001U)
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
static inline unsigned short htons(unsigned short value)
{ return (unsigned short)((value << 8) | (value >> 8)); }
static inline unsigned short ntohs(unsigned short value) { return htons(value); }
static inline unsigned int htonl(unsigned int value)
{ return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
	((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24); }
static inline unsigned int ntohl(unsigned int value) { return htonl(value); }

#endif
