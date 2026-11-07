#define _POSIX_C_SOURCE 200809L
#include "format.h"
#include "io.h"
#include "sha256.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static char package_directory[1024];
static void stop_server(int signal_number)
{
	(void)signal_number;
	stopping = 1;
}
static int reply(int client, int code, const char *type, const void *body, size_t size)
{
	char header[256];
	const char *reason = code == 200   ? "OK"
			     : code == 404 ? "Not Found"
			     : code == 405 ? "Method Not Allowed"
			     : code == 503 ? "Service Unavailable"
					   : "Bad Request";
	int n = snprintf(header, sizeof(header),
			 "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: "
			 "%zu\r\nConnection: close\r\n\r\n",
			 code, reason, type, size);
	if (n < 0 || (size_t)n >= sizeof(header) ||
	    pkg_write_all(client, header, (size_t)n) < 0)
		return -1;
	return pkg_write_all(client, body, size);
}
static uint8_t *load_package(const char *filename, size_t *size,
			     struct pkg_archive *package)
{
	if (!pkg_identifier(filename, PKG_NAME_SIZE + PKG_VERSION_SIZE + 8))
		return NULL;
	char path[1200];
	int n = snprintf(path, sizeof(path), "%s/%s", package_directory, filename);
	if (n < 0 || (size_t)n >= sizeof(path))
		return NULL;
	uint8_t *bytes = pkg_read_file(path, PKG_MAX_SIZE, size);
	if (!bytes)
		return NULL;
	const char *error = pkg_parse(bytes, *size, 0, package);
	char expected[PKG_NAME_SIZE + PKG_VERSION_SIZE + 8];
	if (!error) {
		snprintf(expected, sizeof(expected), "%s-%s.spkg", package->name,
			 package->version);
		if (strcmp(expected, filename))
			error = "archive filename disagrees with metadata";
	}
	if (error) {
		fprintf(stderr, "pkg-server: %s: %s\n", filename, error);
		free(bytes);
		return NULL;
	}
	return bytes;
}
static int compare_entries(const void *a, const void *b)
{
	return strcmp(((const struct pkg_index_entry *)a)->name,
		      ((const struct pkg_index_entry *)b)->name);
}
static char *make_index(size_t *length)
{
	DIR *directory = opendir(package_directory);
	if (!directory)
		return NULL;
	struct pkg_index_entry *entries = calloc(PKG_MAX_PACKAGES, sizeof(*entries));
	if (!entries) {
		closedir(directory);
		return NULL;
	}
	size_t count = 0;
	struct dirent *entry;
	char *index = NULL;
	while ((entry = readdir(directory)) != NULL) {
		size_t n = strlen(entry->d_name);
		if (n < 6 || strcmp(entry->d_name + n - 5, ".spkg"))
			continue;
		if (count == PKG_MAX_PACKAGES)
			goto done;
		struct pkg_archive package;
		size_t size;
		uint8_t *bytes = load_package(entry->d_name, &size, &package);
		if (!bytes)
			goto done;
		struct pkg_index_entry *e = &entries[count++];
		strcpy(e->name, package.name);
		strcpy(e->version, package.version);
		strcpy(e->description, package.description);
		strcpy(e->filename, entry->d_name);
		e->size = (uint32_t)size;
		uint8_t hash[32];
		pkg_sha256(bytes, size, hash);
		pkg_hex(hash, e->hash);
		free(bytes);
	}
	qsort(entries, count, sizeof(*entries), compare_entries);
	for (size_t i = 1; i < count; i++)
		if (!strcmp(entries[i - 1].name, entries[i].name)) {
			fprintf(
			    stderr,
			    "pkg-server: keep exactly one version of %s in _pkges/\n",
			    entries[i].name);
			goto done;
		}
	index = malloc(PKG_INDEX_MAX + 1);
	if (!index)
		goto done;
	strcpy(index, "OSPKG-INDEX-1\n");
	size_t used = strlen(index);
	for (size_t i = 0; i < count; i++) {
		const struct pkg_index_entry *e = &entries[i];
		int n = snprintf(index + used, PKG_INDEX_MAX + 1 - used,
				 "%s\t%s\t%u\t%s\t%s\t%s\n", e->name, e->version,
				 e->size, e->hash, e->filename, e->description);
		if (n < 0 || (size_t)n > PKG_INDEX_MAX - used) {
			free(index);
			index = NULL;
			goto done;
		}
		used += (size_t)n;
	}
	*length = used;
done:
	closedir(directory);
	free(entries);
	return index;
}
static void handle_client(int client)
{
	struct timeval timeout = {5, 0};
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	char request[4097];
	size_t used = 0;
	while (used < sizeof(request) - 1) {
		ssize_t n = read(client, request + used, sizeof(request) - 1 - used);
		if (n < 0 && errno == EINTR && !stopping)
			continue;
		if (n <= 0)
			return;
		used += (size_t)n;
		request[used] = 0;
		if (memchr(request, 0, used))
			break;
		if (strstr(request, "\r\n\r\n")) {
			char *line_end = strstr(request, "\r\n");
			*line_end = 0;
			if (strncmp(request, "GET ", 4)) {
				reply(client, 405, "text/plain", "GET only\n", 9);
				return;
			}
			char *path = request + 4;
			char *space = strchr(path, ' ');
			if (!space || (strcmp(space + 1, "HTTP/1.1") &&
				       strcmp(space + 1, "HTTP/1.0")))
				break;
			*space = 0;
			if (!strcmp(path, "/index")) {
				size_t size;
				char *index = make_index(&size);
				if (!index)
					reply(client, 503, "text/plain",
					      "Invalid repository\n", 19);
				else {
					reply(client, 200, "text/plain", index, size);
					free(index);
				}
				return;
			}
			if (!strncmp(path, "/packages/", 10)) {
				const char *name = path + 10;
				size_t size;
				struct pkg_archive package;
				uint8_t *bytes = load_package(name, &size, &package);
				if (bytes) {
					reply(client, 200, "application/octet-stream",
					      bytes, size);
					free(bytes);
					return;
				}
			}
			reply(client, 404, "text/plain", "Not found\n", 10);
			return;
		}
	}
	reply(client, 400, "text/plain", "Bad request\n", 12);
}
int main(int argc, char **argv)
{
	const char *repo = "src/tools/pkg/repo", *bind_address = "127.0.0.1";
	unsigned long port = 8080;
	for (int i = 1; i < argc; i++) {
		if (i + 1 >= argc) {
			fprintf(stderr, "usage: pkg-server [--repo directory] [--bind "
					"IPv4] [--port number]\n");
			return 2;
		}
		const char *value = argv[++i];
		if (!strcmp(argv[i - 1], "--repo"))
			repo = value;
		else if (!strcmp(argv[i - 1], "--bind"))
			bind_address = value;
		else if (!strcmp(argv[i - 1], "--port")) {
			char *end;
			errno = 0;
			port = strtoul(value, &end, 10);
			if (errno || !*value || *end || port > 65535)
				return 2;
		} else
			return 2;
	}
	int n =
	    snprintf(package_directory, sizeof(package_directory), "%s/_pkges", repo);
	if (n < 0 || (size_t)n >= sizeof(package_directory))
		return 1;
	DIR *directory = opendir(package_directory);
	if (!directory) {
		perror("pkg-server: _pkges");
		return 1;
	}
	closedir(directory);
	signal(SIGPIPE, SIG_IGN);
	struct sigaction action = {0};
	action.sa_handler = stop_server;
	sigemptyset(&action.sa_mask);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) {
		perror("pkg-server: socket");
		return 1;
	}
	int reuse = 1;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	struct sockaddr_in address = {0};
	address.sin_family = AF_INET;
	address.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
		fprintf(stderr, "pkg-server: invalid bind address\n");
		close(listener);
		return 2;
	}
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
	    listen(listener, 16) < 0) {
		perror("pkg-server: listen");
		close(listener);
		return 1;
	}
	socklen_t al = sizeof(address);
	getsockname(listener, (struct sockaddr *)&address, &al);
	printf("pkg-server: http://%s:%u repo=%s\n", bind_address,
	       (unsigned)ntohs(address.sin_port), repo);
	fflush(stdout);
	while (!stopping) {
		int client = accept(listener, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			perror("pkg-server: accept");
			break;
		}
		handle_client(client);
		close(client);
	}
	close(listener);
	return 0;
}
