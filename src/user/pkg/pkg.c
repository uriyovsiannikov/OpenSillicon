#include "format.h"
#include "http.h"
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
#ifdef PKG_HOST
#include <signal.h>
#endif

static const char *root = "";
static const char *repository = "http://10.15.3.2:8080";
static char configured_repository[256];
static struct pkg_index_entry index_entries[PKG_MAX_PACKAGES];
static size_t index_count;
static char target_path[256];

static int rooted(char out[256], const char *path)
{
	int n = snprintf(out, 256, "%s/%s", root, path);
	if (n < 0 || n >= 256) {
		fprintf(stderr, "pkg: path too long: %s\n", path);
		return -1;
	}
	return 0;
}
static int fail(const char *message)
{
	fprintf(stderr, "pkg: %s\n", message);
	return 1;
}
static int ensure_directory(const char *relative)
{
	char path[256];
	if (rooted(path, relative) < 0 || pkg_mkdirs(path) < 0) {
		perror("pkg: create directory");
		return -1;
	}
	return 0;
}
static int record_path(char out[256], const char *name)
{
	char relative[128];
	if (!pkg_identifier(name, PKG_NAME_SIZE))
		return -1;
	snprintf(relative, sizeof(relative), "var/db/pkg/%s.db", name);
	return rooted(out, relative);
}
static int save_file(const char *relative, const void *bytes, size_t length)
{
	char path[256];
	if (rooted(path, relative) < 0)
		return -1;
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
	if (fd < 0)
		return -1;
	int error = pkg_write_all(fd, bytes, length);
	if (close(fd) < 0)
		error = -1;
	return error;
}
static int read_index(void)
{
	char path[256];
	size_t length;
	if (rooted(path, "var/cache/pkg/index") < 0)
		return 1;
	uint8_t *bytes = pkg_read_file(path, PKG_INDEX_MAX, &length);
	if (!bytes)
		return fail("no repository index; run pkg update first");
	if (memchr(bytes, 0, length)) {
		free(bytes);
		return fail("invalid repository index");
	}
	const char *error = pkg_index_parse((char *)bytes, index_entries, &index_count);
	if (error)
		fprintf(stderr, "pkg: %s\n", error);
	free(bytes);
	return error != NULL;
}
static int update_index(void)
{
	printf("Fetching repository index from %s\n", repository);
	size_t size;
	uint8_t *bytes = pkg_http_get(repository, "/index", PKG_INDEX_MAX, &size);
	if (!bytes)
		return 1;
	uint8_t *copy = malloc(size + 1);
	if (!copy) {
		free(bytes);
		return fail("out of memory");
	}
	memcpy(copy, bytes, size + 1);
	const char *error =
	    memchr(bytes, 0, size)
		? "invalid repository index"
		: pkg_index_parse((char *)copy, index_entries, &index_count);
	free(copy);
	int result = 1;
	if (error)
		fprintf(stderr, "pkg: %s\n", error);
	else if (ensure_directory("var/cache/pkg") < 0 ||
		 save_file("var/cache/pkg/index", bytes, size) < 0)
		perror("pkg: save index");
	else {
		printf("Repository updated: %zu packages\n", index_count);
		result = 0;
	}
	free(bytes);
	return result;
}
static int installed_record(const char *name, struct pkg_archive *package,
			    uint8_t **bytes)
{
	char path[256];
	size_t size;
	if (record_path(path, name) < 0)
		return fail("invalid package name");
	*bytes = pkg_read_file(path, PKG_HEADER_SIZE + PKG_MAX_FILES * PKG_ENTRY_SIZE,
			       &size);
	if (!*bytes)
		return fail("package is not installed or its record is unreadable");
	const char *error = pkg_parse(*bytes, size, 1, package);
	if (!error && strcmp(package->name, name))
		error = "package database name mismatch";
	if (error) {
		free(*bytes);
		*bytes = NULL;
		return fail(error);
	}
	return 0;
}
static int list_packages(void)
{
	char path[256];
	if (rooted(path, "var/db/pkg") < 0)
		return 1;
	DIR *directory = opendir(path);
	if (!directory) {
		if (errno == ENOENT) {
			puts("No installed packages");
			return 0;
		}
		perror("pkg: database");
		return 1;
	}
	struct dirent *entry;
	int result = 0;
	unsigned count = 0;
	while ((entry = readdir(directory)) != NULL) {
		size_t n = strlen(entry->d_name);
		if (n < 4 || strcmp(entry->d_name + n - 3, ".db"))
			continue;
		char name[PKG_NAME_SIZE];
		if (n - 3 >= sizeof(name)) {
			result = 1;
			continue;
		}
		memcpy(name, entry->d_name, n - 3);
		name[n - 3] = 0;
		struct pkg_archive package;
		uint8_t *bytes = NULL;
		if (installed_record(name, &package, &bytes)) {
			result = 1;
			continue;
		}
		printf("%s %s%s\n", package.name, package.version,
		       package.installed ? "" : " [incomplete]");
		free(bytes);
		count++;
	}
	closedir(directory);
	if (!count && !result)
		puts("No installed packages");
	return result;
}
static int show_info(const char *name, int verify)
{
	struct pkg_archive package;
	uint8_t *bytes = NULL;
	if (installed_record(name, &package, &bytes))
		return 1;
	if (!verify) {
		printf("Name: %s\nVersion: %s\nDescription: %s\nState: %s\nFiles: %u\n",
		       package.name, package.version, package.description,
		       package.installed ? "installed" : "incomplete", package.count);
		for (uint32_t i = 0; i < package.count; i++)
			printf("/%s\n", package.files[i].path);
		free(bytes);
		return 0;
	}
	int result = package.installed ? 0 : fail("installation is incomplete");
	for (uint32_t i = 0; i < package.count; i++) {
		const struct pkg_file *f = &package.files[i];
		size_t size = 0;
		uint8_t hash[32];
		struct stat st;
		if (rooted(target_path, f->path) < 0) {
			result = 1;
			continue;
		}
		uint8_t *data = pkg_read_file(target_path, PKG_MAX_SIZE, &size);
		if (data)
			pkg_sha256(data, size, hash);
		if (!data || size != f->size || memcmp(hash, f->hash, 32) ||
		    stat(target_path, &st) < 0 || (st.st_mode & 07777) != f->mode) {
			fprintf(stderr, "pkg: verification failed: /%s\n", f->path);
			result = 1;
		}
		free(data);
	}
	if (!result)
		printf("Verified %s: %u files\n", package.name, package.count);
	free(bytes);
	return result;
}
static int preflight(const struct pkg_archive *p)
{
	struct stat st;
	char db[256];
	if (record_path(db, p->name) < 0)
		return fail("invalid database path");
	if (stat(db, &st) == 0)
		return fail(
		    "package already has an installation record (see pkg info)");
	if (errno != ENOENT) {
		perror("pkg: database lookup");
		return 1;
	}
	for (uint32_t i = 0; i < p->count; i++) {
		if (rooted(target_path, p->files[i].path) < 0)
			return 1;
#ifdef PKG_HOST
		int exists = lstat(target_path, &st);
#else
		int exists = stat(target_path, &st);
#endif
		if (exists == 0) {
			fprintf(stderr, "pkg: file already exists: /%s\n",
				p->files[i].path);
			return 1;
		}
		if (errno != ENOENT) {
			perror("pkg: destination lookup");
			return 1;
		}
		/* Check existing parents without creating any payload files. */
		char parent[256];
		strcpy(parent, target_path);
		for (char *s = parent + 1; *s; s++)
			if (*s == '/') {
				*s = 0;
#ifdef PKG_HOST
				exists = lstat(parent, &st);
#else
				exists = stat(parent, &st);
#endif
				if (exists == 0 && !S_ISDIR(st.st_mode))
					return fail(
					    "destination parent is not a directory");
				if (exists < 0 && errno != ENOENT) {
					perror("pkg: parent lookup");
					return 1;
				}
				*s = '/';
			}
	}
	return 0;
}
static int install_archive(const uint8_t *data, size_t size,
			   const struct pkg_index_entry *expected)
{
	struct pkg_archive package;
	const char *error = pkg_parse(data, size, 0, &package);
	if (error)
		return fail(error);
	if (expected && (strcmp(package.name, expected->name) ||
			 strcmp(package.version, expected->version)))
		return fail("downloaded package does not match repository metadata");
	if (preflight(&package))
		return 1;
	if (ensure_directory("var/db/pkg") < 0)
		return 1;
	char db[256];
	if (record_path(db, package.name) < 0)
		return 1;
	/* O_EXCL reserves ownership before any payload is written. Keep pending
	 * records on error so partial installations are visible after a reboot. */
	int record = open(db, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
	if (record < 0) {
		perror("pkg: reserve installation record");
		return 1;
	}
	uint8_t header[PKG_HEADER_SIZE];
	memcpy(header, data, sizeof(header));
	memcpy(header, PKG_RECORD_MAGIC, 8);
	pkg_put_u32(header + 8, 0);
	int result = 1;
	if (pkg_write_all(record, header, sizeof(header)) < 0)
		goto failed;
	for (uint32_t i = 0; i < package.count; i++)
		if (pkg_write_all(record, package.files[i].header, PKG_ENTRY_SIZE) < 0)
			goto failed;
	for (uint32_t i = 0; i < package.count; i++) {
		struct pkg_file *f = &package.files[i];
		char parent[256];
		if (rooted(target_path, f->path) < 0)
			goto failed;
		strcpy(parent, target_path);
		char *slash = strrchr(parent, '/');
		*slash = 0;
		if (pkg_mkdirs(parent) < 0)
			goto failed;
		int fd =
		    open(target_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		if (fd < 0)
			goto failed;
		int written = pkg_write_all(fd, f->data, f->size);
		if (written == 0)
			written = fchmod(fd, f->mode);
		if (close(fd) < 0)
			written = -1;
		if (written < 0)
			goto failed;
	}
	pkg_put_u32(header, 1);
	if (lseek(record, 8, SEEK_SET) != 8 || pkg_write_all(record, header, 4) < 0)
		goto failed;
	printf("Installed %s %s (%u files)\n", package.name, package.version,
	       package.count);
	result = 0;
	goto done;
failed:
	fprintf(stderr, "pkg: installation incomplete (errno %d); record kept at %s\n",
		errno, db);
done:
	if (close(record) < 0)
		result = 1;
	return result;
}
static int install_remote(const char *name)
{
	if (!pkg_identifier(name, PKG_NAME_SIZE))
		return fail("invalid package name");
	if (read_index())
		return 1;
	struct pkg_index_entry *entry = NULL;
	for (size_t i = 0; i < index_count; i++)
		if (!strcmp(name, index_entries[i].name)) {
			entry = &index_entries[i];
			break;
		}
	if (!entry)
		return fail("package not found in repository index");
	char path[160];
	snprintf(path, sizeof(path), "/packages/%s", entry->filename);
	printf("Downloading %s %s (%u bytes)\n", entry->name, entry->version,
	       entry->size);
	size_t size;
	uint8_t *bytes = pkg_http_get(repository, path, PKG_MAX_SIZE, &size);
	if (!bytes)
		return 1;
	uint8_t hash[32];
	char hex[65];
	pkg_sha256(bytes, size, hash);
	pkg_hex(hash, hex);
	int result;
	if (size != entry->size || strcmp(hex, entry->hash))
		result = fail(
		    "repository package SHA-256 or length mismatch; run pkg update");
	else
		result = install_archive(bytes, size, entry);
	free(bytes);
	return result;
}
static void load_repository(void)
{
	char path[256];
	size_t size;
	if (rooted(path, "etc/pkg.conf") < 0)
		return;
	uint8_t *bytes = pkg_read_file(path, sizeof(configured_repository) - 1, &size);
	if (!bytes)
		return;
	while (size && (bytes[size - 1] == '\n' || bytes[size - 1] == '\r'))
		size--;
	memcpy(configured_repository, bytes, size);
	configured_repository[size] = 0;
	free(bytes);
	if (size)
		repository = configured_repository;
}
static int usage(void)
{
	puts("usage: pkg [-r root] [-R http://host:port] command [argument]");
	puts("  update            download repository index");
	puts("  search [text]     search the downloaded index");
	puts("  install name      download, verify and install a new package");
	puts("  add file.spkg     verify and install a local package");
	puts("  list              list installed and incomplete packages");
	puts("  info name         show installed package metadata");
	puts("  verify name       check installed file hashes and modes");
	return 2;
}
int main(int argc, char **argv)
{
#ifdef PKG_HOST
	signal(SIGPIPE, SIG_IGN);
#endif
	const char *repo_override = NULL;
	int arg = 1;
	while (arg < argc && argv[arg][0] == '-') {
		if (!strcmp(argv[arg], "--help")) {
			usage();
			return 0;
		}
		if (arg + 1 >= argc)
			return usage();
		if (!strcmp(argv[arg], "-r")) {
			root = argv[arg + 1];
			if (root[0] != '/')
				return fail("installation root must be absolute");
		} else if (!strcmp(argv[arg], "-R"))
			repo_override = argv[arg + 1];
		else
			return usage();
		arg += 2;
	}
	if (!strcmp(root, "/"))
		root = "";
	load_repository();
	if (repo_override)
		repository = repo_override;
	if (arg == argc)
		return list_packages();
	const char *command = argv[arg++];
	int remaining = argc - arg;
	if (!*root && geteuid() != 0 &&
	    (!strcmp(command, "update") || !strcmp(command, "install") ||
	     !strcmp(command, "add")))
		return fail("system package operations require root; use -r "
			    "/tmp/packages for a writable installation prefix");
	if (!strcmp(command, "update") && remaining == 0)
		return update_index();
	if (!strcmp(command, "list") && remaining == 0)
		return list_packages();
	if ((!strcmp(command, "info") || !strcmp(command, "verify")) && remaining == 1)
		return show_info(argv[arg], !strcmp(command, "verify"));
	if (!strcmp(command, "install") && remaining == 1)
		return install_remote(argv[arg]);
	if (!strcmp(command, "add") && remaining == 1) {
		size_t size;
		uint8_t *bytes = pkg_read_file(argv[arg], PKG_MAX_SIZE, &size);
		if (!bytes) {
			perror("pkg: read archive");
			return 1;
		}
		int result = install_archive(bytes, size, NULL);
		free(bytes);
		return result;
	}
	if (!strcmp(command, "search") && remaining <= 1) {
		if (read_index())
			return 1;
		for (size_t i = 0; i < index_count; i++) {
			struct pkg_index_entry *e = &index_entries[i];
			if (!remaining || strstr(e->name, argv[arg]) ||
			    strstr(e->description, argv[arg]))
				printf("%s %s - %s\n", e->name, e->version,
				       e->description);
		}
		return 0;
	}
	return usage();
}
