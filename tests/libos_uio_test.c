#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>
#include "abi/syscall.h"

/* Exercise the actual wrappers against a stream, enforcing the native
 * syscall limits at the boundary instead of relying on Linux's larger limit. */
long __libos_call(long number, long fd, long address, long count,
                 long unused1, long unused2, long unused3)
{
    const struct iovec *v = (const struct iovec *)address;
    size_t total = 0;
    (void)unused1; (void)unused2; (void)unused3;
    assert(number == SYSCALL_READV || number == SYSCALL_WRITEV);
    if (count < 0 || count > 16) { errno = EINVAL; return -1; }
    for (long i = 0; i < count; i++) total += v[i].iov_len;
    if (total > 65536) { errno = EINVAL; return -1; }
    return syscall(number == SYSCALL_READV ? SYS_readv : SYS_writev,
                   fd, v, count);
}

int main(void)
{
    static unsigned char source[150000], result[150000];
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    for (size_t i = 0; i < sizeof(source); i++) source[i] = (i * 17U) % 251U;
    size_t sent = 0, received = 0;
    while (sent < sizeof(source)) {
        size_t header = sizeof(source) - sent > 12 ? 12 : sizeof(source) - sent;
        struct iovec out[] = {{source + sent, header},
            {source + sent + header, sizeof(source) - sent - header}};
        ssize_t n = writev(sockets[0], out, 2);
        assert(n > 0);
        assert(out[1].iov_len == sizeof(source) - sent - header);
        sent += (size_t)n;
        struct iovec in[] = {{result + received, 7},
            {result + received + 7, sizeof(result) - received - 7}};
        n = readv(sockets[1], in, 2);
        assert(n > 0);
        received += (size_t)n;
    }
    assert(received == sizeof(result));
    assert(memcmp(source, result, sizeof(source)) == 0);
    close(sockets[0]); close(sockets[1]);
    puts("PASS: large vector stream transfers preserve all bytes across retries");
    return 0;
}
