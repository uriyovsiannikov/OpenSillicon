#ifndef LIBOS_ARPA_INET_H
#define LIBOS_ARPA_INET_H
#include <netinet/in.h>
in_addr_t inet_addr(const char *);
int inet_aton(const char *, struct in_addr *);
char *inet_ntoa(struct in_addr);
const char *inet_ntop(int, const void *, char *, socklen_t);
#endif
