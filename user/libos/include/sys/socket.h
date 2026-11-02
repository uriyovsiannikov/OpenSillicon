#ifndef LIBOS_SYS_SOCKET_H
#define LIBOS_SYS_SOCKET_H

#include <sys/types.h>

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

#define AF_UNIX 1
#define AF_INET 2
#define AF_INET6 10
#define AF_UNSPEC 0
#define AF_LOCAL AF_UNIX
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define MSG_OOB 0x01
#define MSG_PEEK 0x02
#define MSG_DONTROUTE 0x04
#define MSG_CTRUNC 0x08
#define MSG_TRUNC 0x20
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_LINGER 13
#define SO_PEERCRED 17
#define SHUT_RD 0
#define SHUT_WR 1
#define SHUT_RDWR 2

struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

struct sockaddr_storage {
	sa_family_t ss_family;
	unsigned char __padding[6];
	unsigned long __align;
	unsigned char __padding2[112];
};

struct sockaddr_un {
	sa_family_t sun_family;
	char sun_path[108];
};

struct ucred {
	pid_t pid;
	unsigned int uid;
	unsigned int gid;
};

int socket(int domain, int type, int protocol);
ssize_t send(int fd, const void *buffer, size_t length, int flags);
ssize_t recv(int fd, void *buffer, size_t length, int flags);
int bind(int fd, const struct sockaddr *address, socklen_t length);
int listen(int fd, int backlog);
int connect(int fd, const struct sockaddr *address, socklen_t length);
int accept(int fd, struct sockaddr *address, socklen_t *length);
int getsockname(int fd, struct sockaddr *address, socklen_t *length);
int getpeername(int fd, struct sockaddr *address, socklen_t *length);
int shutdown(int fd, int how);
int getsockopt(int fd, int level, int option, void *value, socklen_t *length);
int setsockopt(int fd, int level, int option, const void *value,
	socklen_t length);

#endif
