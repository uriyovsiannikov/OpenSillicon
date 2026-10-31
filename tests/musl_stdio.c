#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <termios.h>
#include <unistd.h>

static __thread volatile unsigned long stdio_tls = 0x535444494f544c53UL;

int
main(int argc, char **argv, char **envp)
{
	struct winsize window = { 0 };
	struct iovec vectors[2];
	char first[5] = { 0 };
	char second[8] = { 0 };
	char partial_first[10] = { 0 };
	char partial_second[80] = { 0 };
	char bad_buffer[4] = { 0 };
	char line[64];
	static const char expected_passwd[] =
	    "root:*:0:0:OpenSillicon root:/:/bin/sh\n"
	    "user:*:1000:1000:OpenSillicon user:/tmp:/bin/sh\n";
	char *allocation;
	FILE *input;
	int fd;
	ssize_t amount;

	if (argc != 0 || argv == NULL || argv[0] != NULL || envp == NULL ||
	    envp[0] != NULL || stdio_tls != 0x535444494f544c53UL)
		return 1;
	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
	    !isatty(STDERR_FILENO))
		return 2;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) != 0 ||
	    window.ws_row == 0 || window.ws_col == 0)
		return 3;
	if (ioctl(STDOUT_FILENO, TIOCSWINSZ, &window) != -1 || errno != ENOTTY)
		return 4;
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, (void *)(uintptr_t)1) != -1 ||
	    errno != EFAULT)
		return 5;
	fd = open("/etc/passwd", O_RDONLY);
	if (fd < 0 || ioctl(fd, TIOCGWINSZ, &window) != -1 || errno != ENOTTY ||
	    isatty(fd) != 0 || close(fd) != 0)
		return 21;
	errno = 0;
	if (ioctl(99, TIOCGWINSZ, &window) != -1 || errno != EBADF)
		return 24;

	if (printf("stdio printf: %s %d %u 0x%x\n", "ok", -7, 42U, 42U) < 0 ||
	    fprintf(stdout, "stdio stdout: %s\n", "fprintf") < 0 ||
	    fprintf(stderr, "stdio stderr: %s\n", "fprintf") < 0 ||
	    puts("stdio puts") < 0 || putchar('P') != 'P' || putchar('\n') != '\n' ||
	    fwrite("stdio fwrite\n", 1, 13, stdout) != 13 || fflush(stdout) != 0)
		return 6;

	allocation = malloc(32);
	if (allocation == NULL)
		return 7;
	memset(allocation, 0x5a, 32);
	if (allocation[0] != 0x5a || allocation[31] != 0x5a ||
	    stdio_tls != 0x535444494f544c53UL)
		return 8;
	stdio_tls++;
	free(allocation);

	/* This drives real musl stdio input/readv through the VFS. */
	input = fopen("/etc/passwd", "r");
	if (input == NULL || fgets(line, sizeof(line), input) == NULL ||
	    strncmp(line, "root:*:0:0:", 11) != 0 || fclose(input) != 0)
		return 9;
	fd = open("/etc/passwd", O_RDONLY);
	if (fd < 0)
		return 10;
	vectors[0].iov_base = first;
	vectors[0].iov_len = sizeof(first) - 1;
	vectors[1].iov_base = second;
	vectors[1].iov_len = sizeof(second) - 1;
	amount = readv(fd, vectors, 2);
	if (amount != (ssize_t)(sizeof(first) + sizeof(second) - 2) ||
	    memcmp(first, "root", 4) != 0 || second[0] != ':' ||
	    readv(fd, NULL, 0) != 0 || close(fd) != 0)
		return 11;
	fd = open("/etc/passwd", O_RDONLY);
	if (fd < 0 || read(fd, line, 4) != 4 || memcmp(line, "root", 4) != 0 ||
	    close(fd) != 0)
		return 25;
	fd = open("/etc/passwd", O_RDONLY);
	if (fd < 0)
		return 26;
	vectors[0].iov_base = partial_first;
	vectors[0].iov_len = sizeof(partial_first);
	vectors[1].iov_base = partial_second;
	vectors[1].iov_len = sizeof(partial_second);
	amount = readv(fd, vectors, 2);
	if (amount != (ssize_t)(sizeof(expected_passwd) - 1) ||
	    memcmp(partial_first, expected_passwd, sizeof(partial_first)) != 0 ||
	    memcmp(partial_second, expected_passwd + sizeof(partial_first),
	    sizeof(expected_passwd) - 1 - sizeof(partial_first)) != 0 ||
	    readv(fd, vectors, 2) != 0 || close(fd) != 0)
		return 27;

	/* Invalid calls exercise the native syscall adapter and kernel checks. */
	errno = 0;
	if (syscall(SYS_writev, STDOUT_FILENO, (void *)(uintptr_t)1, 1) != -1 ||
	    errno != EFAULT)
		return 12;
	errno = 0;
	if (syscall(SYS_writev, 99, vectors, 0) != -1 || errno != EBADF)
		return 13;
	errno = 0;
	if (syscall(SYS_writev, STDOUT_FILENO, vectors, 17) != -1 ||
	    errno != EINVAL)
		return 14;
	if (stdio_tls != 0x535444494f544c54UL)
		return 15;
	if (writev(STDOUT_FILENO, NULL, 0) != 0)
		return 22;
	vectors[0].iov_base = (void *)"stdio writev: ";
	vectors[0].iov_len = 14;
	vectors[1].iov_base = (void *)"ok\n";
	vectors[1].iov_len = 3;
	if (writev(STDOUT_FILENO, vectors, 2) != 17)
		return 16;
	vectors[0].iov_base = (void *)(uintptr_t)1;
	vectors[0].iov_len = 1;
	errno = 0;
	if (syscall(SYS_writev, STDOUT_FILENO, vectors, 1) != -1 ||
	    errno != EFAULT)
		return 17;
	vectors[0].iov_base = bad_buffer;
	vectors[0].iov_len = (size_t)-1;
	vectors[1].iov_base = bad_buffer;
	vectors[1].iov_len = 2;
	errno = 0;
	if (syscall(SYS_writev, STDOUT_FILENO, vectors, 2) != -1 ||
	    errno != EINVAL)
		return 18;
	vectors[0].iov_base = (void *)(uintptr_t)1;
	vectors[0].iov_len = 1;
	errno = 0;
	if (syscall(SYS_readv, STDIN_FILENO, vectors, 1) != -1 ||
	    errno != EFAULT)
		return 19;
	errno = 0;
	if (syscall(SYS_readv, STDIN_FILENO, (void *)(uintptr_t)1, 1) != -1 ||
	    errno != EFAULT)
		return 23;
	errno = 0;
	if (syscall(SYS_readv, STDIN_FILENO, vectors, 17) != -1 ||
	    errno != EINVAL)
		return 28;
	vectors[0].iov_base = bad_buffer;
	vectors[0].iov_len = 65536;
	vectors[1].iov_base = bad_buffer;
	vectors[1].iov_len = 1;
	errno = 0;
	if (syscall(SYS_readv, STDIN_FILENO, vectors, 2) != -1 ||
	    errno != EINVAL)
		return 29;
	if (readv(STDIN_FILENO, NULL, 0) != 0)
		return 30;
	puts("OpenSillicon: musl stdio, TTY query, VFS readv, allocator, and TLS passed");
	return fflush(stdout) == 0 ? 0 : 20;
}
