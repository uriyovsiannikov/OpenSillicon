#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(sizeof(ino_t) == 8, "musl x86_64 ino_t must be 64-bit");
_Static_assert(sizeof(off_t) == 8, "musl x86_64 off_t must be 64-bit");
_Static_assert(offsetof(struct dirent, d_ino) == 0, "dirent d_ino offset");
_Static_assert(offsetof(struct dirent, d_off) == 8, "dirent d_off offset");
_Static_assert(offsetof(struct dirent, d_reclen) == 16, "dirent reclen offset");
_Static_assert(offsetof(struct dirent, d_type) == 18, "dirent type offset");
_Static_assert(offsetof(struct dirent, d_name) == 19, "dirent name offset");
_Static_assert(sizeof(struct dirent) == 280, "musl x86_64 dirent size");

#define ALPHA_NAME "musl_dir_alpha"
#define BETA_NAME "musl_dir_beta"

static _Thread_local volatile int tls_probe;

union aligned_buffer {
	uint64_t words[512];
	char bytes[4096];
};

static int
fail(int code)
{
	return code;
}

static unsigned int
name_bit(const char *name)
{
	if (strcmp(name, ".") == 0)
		return 1U;
	if (strcmp(name, "..") == 0)
		return 2U;
	if (strcmp(name, ALPHA_NAME) == 0)
		return 4U;
	if (strcmp(name, BETA_NAME) == 0)
		return 8U;
	return 0;
}

static int
check_record(const struct dirent *entry, size_t available,
	unsigned int *seen, int64_t *last_offset, uint64_t dot_inode,
	uint64_t parent_inode, uint64_t alpha_inode, uint64_t beta_inode)
{
	const char *terminator;
	size_t name_bytes;
	size_t expected_length;
	unsigned int bit;
	uint64_t expected_inode;
	unsigned char expected_type;

	if (entry->d_reclen < offsetof(struct dirent, d_name) + 1U ||
	    (entry->d_reclen & 7U) != 0 || entry->d_reclen > available)
		return 0;
	terminator = memchr(entry->d_name, '\0',
	    entry->d_reclen - offsetof(struct dirent, d_name));
	if (terminator == NULL)
		return 0;
	name_bytes = (size_t)(terminator - entry->d_name);
	if (name_bytes == 0 || name_bytes > NAME_MAX)
		return 0;
	expected_length = (offsetof(struct dirent, d_name) + name_bytes + 1U + 7U) &
	    ~(size_t)7U;
	if (entry->d_reclen != expected_length)
		return 0;
	bit = name_bit(entry->d_name);
	if (bit == 0 || (*seen & bit) != 0 || entry->d_ino == 0 ||
	    entry->d_off != *last_offset + 1)
		return 0;
	if (bit == 1U) {
		expected_inode = dot_inode;
		expected_type = DT_DIR;
	} else if (bit == 2U) {
		expected_inode = parent_inode;
		expected_type = DT_DIR;
	} else if (bit == 4U) {
		expected_inode = alpha_inode;
		expected_type = DT_REG;
	} else {
		expected_inode = beta_inode;
		expected_type = DT_REG;
	}
	if (entry->d_ino != expected_inode || entry->d_type != expected_type)
		return 0;
	*seen |= bit;
	*last_offset = entry->d_off;
	return 1;
}

static int
check_batch(char *buffer, int length, unsigned int *seen, int64_t *last_offset,
	uint64_t dot_inode, uint64_t parent_inode, uint64_t alpha_inode,
	uint64_t beta_inode)
{
	size_t offset = 0;

	while (offset < (size_t)length) {
		const struct dirent *entry = (const struct dirent *)(buffer + offset);
		if (!check_record(entry, (size_t)length - offset, seen, last_offset,
		    dot_inode, parent_inode, alpha_inode, beta_inode))
			return 0;
		offset += entry->d_reclen;
	}
	return offset == (size_t)length;
}

int
main(void)
{
	static const char alpha_path[] = "/musl-getdents/" ALPHA_NAME;
	static const char beta_path[] = "/musl-getdents/" BETA_NAME;
	static union aligned_buffer buffer;
	struct stat alpha_status;
	struct stat beta_status;
	struct stat directory_status;
	struct stat parent_status;
	DIR *stream;
	DIR *independent;
	struct dirent *entry;
	unsigned int seen = 0;
	unsigned int independent_seen = 0;
	int64_t last_offset = 0;
	int64_t independent_offset = 0;
	int alpha_fd;
	int beta_fd;
	int directory_fd;
	int independent_fd;
	int parent_fd;
	int regular_fd;
	int result;

	tls_probe = 1;
	if (tls_probe != 1)
		return fail(19);
	alpha_fd = open(alpha_path, O_RDONLY);
	beta_fd = open(beta_path, O_RDONLY);
	directory_fd = open("/musl-getdents", O_RDONLY | O_DIRECTORY);
	independent_fd = open("/musl-getdents", O_RDONLY | O_DIRECTORY);
	if (alpha_fd < 0 || beta_fd < 0 || directory_fd < 0 || independent_fd < 0 ||
	    fstat(alpha_fd, &alpha_status) != 0 || fstat(beta_fd, &beta_status) != 0 ||
	    fstat(directory_fd, &directory_status) != 0 ||
	    !S_ISDIR(directory_status.st_mode))
		return fail(21);
	parent_fd = open("/", O_RDONLY | O_DIRECTORY);
	if (parent_fd < 0 || fstat(parent_fd, &parent_status) != 0 ||
	    close(parent_fd) != 0)
		return fail(21);

	result = getdents(-1, (struct dirent *)buffer.bytes, 24);
	if (result != -1)
		return fail(122);
	if (errno != EBADF)
		return fail(130 + errno);
	regular_fd = alpha_fd;
	if (getdents(regular_fd, (struct dirent *)buffer.bytes, 24) != -1 ||
	    errno != ENOTDIR)
		return fail(23);
	if (getdents(directory_fd, (struct dirent *)buffer.bytes, 23) != -1 ||
	    errno != EINVAL)
		return fail(24);
	if (getdents(independent_fd, (struct dirent *)(uintptr_t)1, 24) != -1 ||
	    errno != EFAULT)
		return fail(25);
	result = getdents(independent_fd, (struct dirent *)buffer.bytes, 24);
	if (result != 24 || !check_batch(buffer.bytes, result, &independent_seen,
	    &independent_offset, directory_status.st_ino, parent_status.st_ino,
	    alpha_status.st_ino, beta_status.st_ino) || independent_seen != 1U ||
	    independent_offset != 1)
		return fail(26);
	if (getdents(directory_fd, (struct dirent *)buffer.bytes, 24) != 24 ||
	    !check_batch(buffer.bytes, 24, &seen, &last_offset,
	    directory_status.st_ino, parent_status.st_ino, alpha_status.st_ino,
	    beta_status.st_ino) || seen != 1U || last_offset != 1 ||
	    getdents(directory_fd, (struct dirent *)buffer.bytes, 24) != 24 ||
	    !check_batch(buffer.bytes, 24, &seen, &last_offset,
	    directory_status.st_ino, parent_status.st_ino, alpha_status.st_ino,
	    beta_status.st_ino) || seen != 3U || last_offset != 2)
		return fail(27);
	if (getdents(directory_fd, (struct dirent *)buffer.bytes, 24) != -1 ||
	    errno != EINVAL || last_offset != 2)
		return fail(28);
	result = getdents(directory_fd, (struct dirent *)buffer.bytes, 40);
	if (result != 40 || !check_batch(buffer.bytes, result, &seen, &last_offset,
	    directory_status.st_ino, parent_status.st_ino, alpha_status.st_ino,
	    beta_status.st_ino) || seen != 7U || last_offset != 3)
		return fail(130);
	result = getdents(directory_fd, (struct dirent *)buffer.bytes, 40);
	if (result != 40 || !check_batch(buffer.bytes, result, &seen, &last_offset,
	    directory_status.st_ino, parent_status.st_ino, alpha_status.st_ino,
	    beta_status.st_ino) || seen != 15U || last_offset != 4)
		return fail(131);
	if (getdents(directory_fd, (struct dirent *)buffer.bytes,
	    sizeof(buffer.bytes)) != 0 ||
	    getdents(directory_fd, (struct dirent *)buffer.bytes,
	    sizeof(buffer.bytes)) != 0)
		return fail(30);
	result = getdents(independent_fd, (struct dirent *)buffer.bytes, 24);
	if (result != 24 || !check_batch(buffer.bytes, result, &independent_seen,
	    &independent_offset, directory_status.st_ino, parent_status.st_ino,
	    alpha_status.st_ino, beta_status.st_ino) || independent_seen != 3U ||
	    independent_offset != 2)
		return fail(31);
	if (lseek(directory_fd, 0, SEEK_SET) != -1 || errno != ESPIPE ||
	    close(directory_fd) != 0 || close(independent_fd) != 0 ||
	    close(alpha_fd) != 0 || close(beta_fd) != 0)
		return fail(32);

	seen = 0;
	last_offset = 0;
	stream = opendir("/musl-getdents");
	independent = opendir("/musl-getdents");
	if (stream == NULL || independent == NULL)
		return fail(33);
	entry = readdir(stream);
	if (entry == NULL || !check_record(entry, entry->d_reclen, &seen,
	    &last_offset, directory_status.st_ino, parent_status.st_ino,
	    alpha_status.st_ino, beta_status.st_ino))
		return fail(34);
	independent_offset = 0;
	independent_seen = 0;
	entry = readdir(independent);
	if (entry == NULL || !check_record(entry, entry->d_reclen,
	    &independent_seen, &independent_offset, directory_status.st_ino,
	    parent_status.st_ino, alpha_status.st_ino, beta_status.st_ino) ||
	    independent_seen != 1U || telldir(independent) != 1)
		return fail(35);
	if (closedir(independent) != 0)
		return fail(36);
	errno = 0;
	for (;;) {
		entry = readdir(stream);
		if (entry == NULL)
			break;
		if (!check_record(entry, entry->d_reclen, &seen, &last_offset,
		    directory_status.st_ino, parent_status.st_ino, alpha_status.st_ino,
		    beta_status.st_ino))
			return fail(37);
	}
	if (seen != 15U || last_offset != 4 || telldir(stream) != 4 || errno != 0 ||
	    closedir(stream) != 0)
		return fail(38);
	return 0;
}
