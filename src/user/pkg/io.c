#include "io.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int pkg_write_all(int fd, const void *buffer, size_t length)
{
	const uint8_t *p = buffer;
	while (length) {
		size_t chunk = length > 4096 ? 4096 : length;
		ssize_t n = write(fd, p, chunk);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return -1;
		p += n;
		length -= (size_t)n;
	}
	return 0;
}
uint8_t *pkg_read_file(const char *path, size_t limit, size_t *length)
{
	int fd = open(path, O_RDONLY | O_NOFOLLOW);
	if (fd < 0)
		return NULL;
	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    (uint64_t)st.st_size > limit) {
		close(fd);
		errno = EINVAL;
		return NULL;
	}
	size_t size = (size_t)st.st_size;
	uint8_t *bytes = malloc(size + 1);
	if (!bytes) {
		close(fd);
		return NULL;
	}
	size_t done = 0;
	while (done < size) {
		size_t chunk = size - done > 4096 ? 4096 : size - done;
		ssize_t n = read(fd, bytes + done, chunk);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			free(bytes);
			close(fd);
			errno = EIO;
			return NULL;
		}
		done += (size_t)n;
	}
	uint8_t extra;
	ssize_t tail = read(fd, &extra, 1);
	close(fd);
	if (tail != 0) {
		free(bytes);
		errno = EIO;
		return NULL;
	}
	bytes[size] = 0;
	*length = size;
	return bytes;
}
int pkg_mkdirs(const char *path)
{
	char part[256];
	size_t n = strlen(path);
	if (!n || n >= sizeof(part)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(part, path, n + 1);
	for (size_t i = 1; i <= n; i++)
		if (part[i] == '/' || part[i] == 0) {
			char saved = part[i];
			part[i] = 0;
			struct stat st;
		/* Existing parents may be readable but not writable. Do not
		 * require permission to recreate directories that already exist. */
#ifdef PKG_HOST
			int result = lstat(part, &st);
#else
			int result = stat(part, &st);
#endif
			if (result < 0) {
				if (errno != ENOENT ||
				    (mkdir(part, 0755) < 0 && errno != EEXIST))
					return -1;
#ifdef PKG_HOST
				result = lstat(part, &st);
#else
				result = stat(part, &st);
#endif
			}
			if (result < 0 || !S_ISDIR(st.st_mode)) {
				errno = ENOTDIR;
				return -1;
			}
			part[i] = saved;
		}
	return 0;
}
