#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

static __thread unsigned long initialized_tls = 0x4d55534c12345678UL;
static __thread volatile unsigned long zero_tls;

int
main(int argc, char **argv, char **envp)
{
	static const char message[] =
	    "OpenSillicon: native musl write, errno, and TLS passed\n";
	ssize_t written;
	int fd;
	unsigned long expected = initialized_tls + 1;

	if (argc != 0 || argv == NULL || argv[0] != NULL || envp == NULL ||
	    envp[0] != NULL)
		return 21;
	if (initialized_tls != 0x4d55534c12345678UL || zero_tls != 0)
		return 22;
	initialized_tls = expected;
	errno = 0;
	fd = open("/opensillicon-musl-no-such-file", O_RDONLY);
	if (fd != -1)
		return 23;
	if (errno != ENOENT)
		return 100 + errno;
	written = write(STDOUT_FILENO, message, sizeof(message) - 1);
	if (written != (ssize_t)(sizeof(message) - 1))
		return 24;
	if (initialized_tls != expected || zero_tls != 0)
		return 25;
	return 0;
}
