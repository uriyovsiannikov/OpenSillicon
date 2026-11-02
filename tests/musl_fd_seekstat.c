#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

_Static_assert(sizeof(off_t) == 8, "OpenSillicon musl off_t must be 64-bit");
_Static_assert(sizeof(struct stat) == 144,
	"OpenSillicon musl x86_64 struct stat must be 144 bytes");
_Static_assert(offsetof(struct stat, st_size) == 48,
	"OpenSillicon musl st_size ABI offset mismatch");
_Static_assert(offsetof(struct stat, st_atim) == 72,
	"OpenSillicon musl timestamp ABI offset mismatch");

static _Thread_local volatile int tls_probe;

static int
fail(int code)
{
	return code;
}

int
main(int argc, char **argv, char **envp)
{
	static const char contents[] = "0123456789abcdef";
	char bytes[4] = {0};
	struct stat first;
	struct stat second;
	struct stat directory;
	struct stat path_status;
	struct stat relative_status;
	struct stat fd_relative_status;
	struct stat directory_path_status;
	int fd;
	int dirfd;

	tls_probe = 1;
	if (argc != 0 || argv == NULL || argv[0] != NULL || envp == NULL ||
	    envp[0] != NULL || tls_probe != 1)
		return fail(20);
	fd = open("/tmp/musl_seekstat_data", O_CREAT | O_TRUNC | O_RDWR, 0600);
	if (fd < 0)
		return fail(21);
	if (lseek(fd, 0, SEEK_CUR) != 0)
		return fail(22);
	if (write(fd, contents, sizeof(contents) - 1) !=
	    (ssize_t)(sizeof(contents) - 1))
		return fail(23);
	if (lseek(fd, 5, SEEK_SET) != 5 || read(fd, bytes, 3) != 3 ||
	    bytes[0] != '5' || bytes[1] != '6' || bytes[2] != '7')
		return fail(24);
	if (lseek(fd, -3, SEEK_CUR) != 5 || read(fd, bytes, 1) != 1 ||
	    bytes[0] != '5')
		return fail(25);
	if (lseek(fd, -2, SEEK_END) != 14 || read(fd, bytes, 2) != 2 ||
	    bytes[0] != 'e' || bytes[1] != 'f')
		return fail(26);
	if (lseek(fd, 0, SEEK_END) != (off_t)(sizeof(contents) - 1))
		return fail(27);
	if (fstat(fd, &first) != 0 || !S_ISREG(first.st_mode) ||
	    first.st_size != (off_t)(sizeof(contents) - 1) || first.st_ino == 0 ||
	    (first.st_mode & 07777) != 0600)
		return fail(28);
	if (lseek(fd, 0, SEEK_CUR) != first.st_size || fstat(fd, &second) != 0 ||
	    second.st_ino != first.st_ino || second.st_size != first.st_size ||
	    second.st_mode != first.st_mode || lseek(fd, 0, SEEK_CUR) != first.st_size)
		return fail(29);
	/* This VFS does not track ownership, devices, link counts, block usage,
	 * or wall-clock timestamps; zero is its documented unavailable sentinel. */
	if (first.st_dev != 0 || first.st_nlink != 0 || first.st_uid != 0 ||
	    first.st_gid != 0 || first.st_rdev != 0 || first.st_blksize != 0 ||
	    first.st_blocks != 0 || first.st_atim.tv_sec != 0 ||
	    first.st_atim.tv_nsec != 0 || first.st_mtim.tv_sec != 0 ||
	    first.st_mtim.tv_nsec != 0 || first.st_ctim.tv_sec != 0 ||
	    first.st_ctim.tv_nsec != 0)
		return fail(30);
	if (stat("/tmp/musl_seekstat_data", &path_status) != 0)
		return fail(80 + errno);
	if (!S_ISREG(path_status.st_mode) || path_status.st_ino != first.st_ino ||
	    path_status.st_size != first.st_size || path_status.st_mode != first.st_mode)
		return fail(88);
	if (stat("tmp/musl_seekstat_data", &relative_status) != 0 ||
	    relative_status.st_ino != first.st_ino ||
	    relative_status.st_size != first.st_size ||
	    relative_status.st_mode != first.st_mode)
		return fail(39);
	if (stat("/tmp", &directory_path_status) != 0 ||
	    !S_ISDIR(directory_path_status.st_mode) ||
	    directory_path_status.st_ino == 0)
		return fail(40);
	if (stat("/tmp/musl_seekstat_missing", &path_status) != -1 ||
	    errno != ENOENT || stat((const char *)(uintptr_t)1, &path_status) != -1 ||
	    errno != EFAULT || stat("/tmp/musl_seekstat_data",
	    (struct stat *)(uintptr_t)1) != -1 || errno != EFAULT)
		return fail(41);
	if (lseek(fd, -1, SEEK_SET) != -1 || errno != EINVAL ||
	    lseek(fd, -17, SEEK_END) != -1 || errno != EINVAL ||
	    lseek(fd, 0, 99) != -1 || errno != EINVAL)
		return fail(31);
	if (lseek(fd, INT64_MAX, SEEK_CUR) != -1 || errno != EOVERFLOW ||
	    lseek(fd, 0, SEEK_CUR) != first.st_size)
		return fail(32);
	if (lseek(-1, 0, SEEK_SET) != -1 || errno != EBADF ||
	    fstat(-1, &first) != -1 || errno != EBADF)
		return fail(33);
	if (fstat(fd, (struct stat *)(uintptr_t)1) != -1 || errno != EFAULT ||
	    syscall(SYS_fstat, fd, (struct stat *)(uintptr_t)1) != -1 ||
	    errno != EFAULT || lseek(fd, 0, SEEK_CUR) != first.st_size)
		return fail(34);
	if (lseek(0, 0, SEEK_SET) != -1 || errno != ESPIPE)
		return fail(35);
	dirfd = open("/tmp", O_RDONLY | O_DIRECTORY);
	if (dirfd < 0 || fstat(dirfd, &directory) != 0 ||
	    !S_ISDIR(directory.st_mode) || directory.st_ino == 0 ||
	    lseek(dirfd, 0, SEEK_SET) != -1 || errno != ESPIPE)
		return fail(36);
	if (directory_path_status.st_ino != directory.st_ino ||
	    directory_path_status.st_size != directory.st_size ||
	    directory_path_status.st_mode != directory.st_mode)
		return fail(40);
	if (fstatat(dirfd, "musl_seekstat_data", &fd_relative_status, 0) != 0 ||
	    fd_relative_status.st_ino != first.st_ino ||
	    fd_relative_status.st_size != first.st_size ||
	    fd_relative_status.st_mode != first.st_mode ||
	    fstatat(AT_FDCWD, "tmp/musl_seekstat_data", &relative_status, 0) != 0 ||
	    relative_status.st_ino != first.st_ino ||
	    fstatat(-1, "/tmp/musl_seekstat_data", &path_status, 0) != 0 ||
	    path_status.st_ino != first.st_ino)
		return fail(42);
	if (fstatat(dirfd, "musl_seekstat_data", &path_status,
	    AT_SYMLINK_NOFOLLOW) != -1 || errno != EOPNOTSUPP ||
	    fstatat(dirfd, "musl_seekstat_data", &path_status, 0x40000000) != -1 ||
	    errno != EINVAL || fstatat(-1, "musl_seekstat_data", &path_status, 0) != -1 ||
	    errno != EBADF || fstatat(dirfd, "missing", &path_status, 0) != -1 ||
	    errno != ENOENT || fstatat(dirfd, "musl_seekstat_data",
	    (struct stat *)(uintptr_t)1, 0) != -1 || errno != EFAULT)
		return fail(43);
	if (close(dirfd) != 0 || close(fd) != 0)
		return fail(37);
	return 0;
}
