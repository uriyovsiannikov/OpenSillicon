#define _POSIX_C_SOURCE 200809L
#include "format.h"
#include "io.h"
#include "sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char files[PKG_MAX_FILES][PKG_PATH_SIZE];
static size_t count;
static int collect(const char *stage, const char *relative)
{
	char path[1024];
	int n = snprintf(path, sizeof(path), "%s/%s", stage, relative);
	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;
	struct stat st;
	if (lstat(path, &st) < 0)
		return -1;
	if (S_ISREG(st.st_mode)) {
		if (count == PKG_MAX_FILES || !pkg_path(relative) ||
		    (st.st_mode & 07000))
			return -1;
		strcpy(files[count++], relative);
		return 0;
	}
	if (!S_ISDIR(st.st_mode))
		return -1;
	DIR *dir = opendir(path);
	if (!dir)
		return -1;
	struct dirent *entry;
	int result = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		char next[PKG_PATH_SIZE];
		n = snprintf(next, sizeof(next), "%s%s%s", relative,
			     *relative ? "/" : "", entry->d_name);
		if (n < 0 || (size_t)n >= sizeof(next) || collect(stage, next) < 0) {
			result = -1;
			break;
		}
	}
	closedir(dir);
	return result;
}
static int compare(const void *a, const void *b)
{
	return strcmp(a, b);
}
int main(int argc, char **argv)
{
	if (argc < 5 || argc > 6) {
		fprintf(stderr, "usage: pkg-build name version staging-directory "
				"output.spkg [description]\n");
		return 2;
	}
	if (!pkg_identifier(argv[1], PKG_NAME_SIZE) ||
	    !pkg_identifier(argv[2], PKG_VERSION_SIZE)) {
		fprintf(stderr, "pkg-build: invalid name or version\n");
		return 1;
	}
	const char *description = argc == 6 ? argv[5] : argv[1];
	if (strlen(description) >= PKG_DESCRIPTION_SIZE) {
		fprintf(stderr, "pkg-build: description too long\n");
		return 1;
	}
	if (collect(argv[3], "") < 0 || !count) {
		fprintf(stderr, "pkg-build: staging must contain 1..128 regular files "
				"under bin/, usr/, opt/; no links or special files\n");
		return 1;
	}
	qsort(files, count, sizeof(files[0]), compare);
	uint8_t *archive = calloc(1, PKG_MAX_SIZE);
	if (!archive)
		return 1;
	memcpy(archive, PKG_ARCHIVE_MAGIC, 8);
	pkg_put_u32(archive + 12, (uint32_t)count);
	strcpy((char *)archive + 16, argv[1]);
	strcpy((char *)archive + 80, argv[2]);
	strcpy((char *)archive + 112, description);
	size_t used = PKG_HEADER_SIZE;
	int result = 1;
	for (size_t i = 0; i < count; i++) {
		char path[1024];
		int path_length =
		    snprintf(path, sizeof(path), "%s/%.191s", argv[3], files[i]);
		if (path_length < 0 || (size_t)path_length >= sizeof(path))
			goto done;
		size_t size;
		uint8_t *data = pkg_read_file(path, PKG_MAX_SIZE, &size);
		struct stat st;
		if (!data || stat(path, &st) < 0 ||
		    used + PKG_ENTRY_SIZE + size > PKG_MAX_SIZE) {
			free(data);
			fprintf(stderr,
				"pkg-build: file unreadable or package too large: %s\n",
				path);
			goto done;
		}
		uint8_t *header = archive + used;
		strcpy((char *)header, files[i]);
		pkg_put_u32(header + 192, (st.st_mode & 0111) ? 0755 : 0644);
		pkg_put_u32(header + 196, (uint32_t)size);
		pkg_sha256(data, size, header + 200);
		memcpy(header + PKG_ENTRY_SIZE, data, size);
		free(data);
		used += PKG_ENTRY_SIZE + size;
	}
	pkg_put_u32(archive + 8, (uint32_t)used);
	struct pkg_archive package;
	const char *error = pkg_parse(archive, used, 0, &package);
	if (error) {
		fprintf(stderr, "pkg-build: %s\n", error);
		goto done;
	}
	char temporary[1024];
	int n = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", argv[4]);
	if (n < 0 || (size_t)n >= sizeof(temporary))
		goto done;
	int fd = mkstemp(temporary);
	if (fd < 0) {
		perror("pkg-build: output");
		goto done;
	}
	int written = pkg_write_all(fd, archive, used);
	if (written == 0)
		written = fchmod(fd, 0644);
	if (written == 0)
		written = fsync(fd);
	if (close(fd) < 0)
		written = -1;
	if (written < 0 || rename(temporary, argv[4]) < 0) {
		perror("pkg-build: publish");
		unlink(temporary);
		goto done;
	}
	printf("Built %s %s: %zu files, %zu bytes -> %s\n", argv[1], argv[2], count,
	       used, argv[4]);
	result = 0;
done:
	free(archive);
	return result;
}
