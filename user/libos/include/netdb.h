#ifndef LIBOS_NETDB_H
#define LIBOS_NETDB_H

#include <sys/socket.h>

struct addrinfo {
	int ai_flags, ai_family, ai_socktype, ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};
struct hostent {
	char *h_name;
	char **h_aliases;
	int h_addrtype, h_length;
	char **h_addr_list;
};
#define EAI_NONAME (-2)
#define EAI_AGAIN (-3)
#define AI_PASSIVE 0x0001
#define AI_CANONNAME 0x0002
#define AI_NUMERICHOST 0x0004
#define AI_NUMERICSERV 0x0400
int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);

#endif
