#include "format.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

uint32_t pkg_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}
void pkg_put_u32(uint8_t *p, uint32_t n)
{
	for (unsigned i = 0; i < 4; i++)
		p[i] = (uint8_t)(n >> (i * 8));
}
int pkg_identifier(const char *s, size_t capacity)
{
	size_t n = strnlen(s, capacity);
	if (!n || n == capacity || s[0] == '.' || s[0] == '-')
		return 0;
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
		      c == '+'))
			return 0;
	}
	return 1;
}
static int printable(const char *s, size_t cap)
{
	size_t n = strnlen(s, cap);
	if (n == cap)
		return 0;
	for (size_t i = 0; i < n; i++)
		if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126)
			return 0;
	return 1;
}
int pkg_path(const char *path)
{
	size_t n = strnlen(path, PKG_PATH_SIZE);
	if (!n || n == PKG_PATH_SIZE || path[0] == '/' || path[n - 1] == '/')
		return 0;
	/* Packages can add applications/data, never the kernel, configuration or DB. */
	if (strncmp(path, "bin/", 4) && strncmp(path, "usr/", 4) &&
	    strncmp(path, "opt/", 4))
		return 0;
	const char *component = path;
	for (size_t i = 0; i <= n; i++) {
		unsigned char c = (unsigned char)path[i];
		if (c == 0 || c == '/') {
			size_t length = (size_t)(path + i - component);
			if (!length || (length == 1 && component[0] == '.') ||
			    (length == 2 && component[0] == '.' && component[1] == '.'))
				return 0;
			component = path + i + 1;
		} else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			     (c >= '0' && c <= '9') || c == '.' || c == '_' ||
			     c == '-' || c == '+'))
			return 0;
	}
	return 1;
}
const char *pkg_parse(const uint8_t *bytes, size_t size, int record,
		      struct pkg_archive *out)
{
	if (size < PKG_HEADER_SIZE || size > PKG_MAX_SIZE)
		return "invalid package length";
	if (memcmp(bytes, record ? PKG_RECORD_MAGIC : PKG_ARCHIVE_MAGIC, 8))
		return "unsupported package format";
	if (!record && pkg_u32(bytes + 8) != size)
		return "package length mismatch";
	if (record && pkg_u32(bytes + 8) > 1)
		return "invalid installation state";
	out->name = (const char *)bytes + 16;
	out->version = (const char *)bytes + 80;
	out->description = (const char *)bytes + 112;
	out->count = pkg_u32(bytes + 12);
	out->installed = record ? pkg_u32(bytes + 8) : 0;
	if (!pkg_identifier(out->name, PKG_NAME_SIZE) ||
	    !pkg_identifier(out->version, PKG_VERSION_SIZE) ||
	    !printable(out->description, PKG_DESCRIPTION_SIZE))
		return "invalid package metadata";
	if (!out->count || out->count > PKG_MAX_FILES)
		return "invalid package file count";
	size_t offset = PKG_HEADER_SIZE;
	for (uint32_t i = 0; i < out->count; i++) {
		if (size - offset < PKG_ENTRY_SIZE)
			return "truncated file header";
		struct pkg_file *f = &out->files[i];
		f->header = bytes + offset;
		f->path = (const char *)f->header;
		f->mode = pkg_u32(f->header + 192);
		f->size = pkg_u32(f->header + 196);
		f->hash = f->header + 200;
		if (!pkg_path(f->path))
			return "unsafe package path";
		if (f->mode != 0644 && f->mode != 0755)
			return "unsupported file mode";
		if (f->size > PKG_MAX_SIZE)
			return "file exceeds size limit";
		for (uint32_t j = 0; j < i; j++) {
			const char *a = out->files[j].path;
			size_t an = strlen(a), bn = strlen(f->path);
			if (!strcmp(a, f->path) ||
			    (an < bn && !strncmp(a, f->path, an) &&
			     f->path[an] == '/') ||
			    (bn < an && !strncmp(a, f->path, bn) && a[bn] == '/'))
				return "duplicate or conflicting package paths";
		}
		offset += PKG_ENTRY_SIZE;
		f->data = record ? NULL : bytes + offset;
		if (!record) {
			uint8_t hash[32];
			if (f->size > size - offset)
				return "truncated file data";
			pkg_sha256(f->data, f->size, hash);
			if (memcmp(hash, f->hash, 32))
				return "file SHA-256 mismatch";
			offset += f->size;
		}
	}
	return offset == size ? NULL : "unexpected trailing data";
}
static int copy_field(char *target, size_t capacity, const char *text)
{
	size_t n = strlen(text);
	if (n >= capacity)
		return 0;
	memcpy(target, text, n + 1);
	return 1;
}
const char *pkg_index_parse(char *text, struct pkg_index_entry *entries, size_t *count)
{
	const char magic[] = "OSPKG-INDEX-1\n";
	*count = 0;
	if (strncmp(text, magic, sizeof(magic) - 1))
		return "unsupported repository index";
	char *line = text + sizeof(magic) - 1;
	while (*line) {
		if (*count == PKG_MAX_PACKAGES)
			return "repository has too many packages";
		char *end = strchr(line, '\n');
		if (!end)
			return "truncated repository index";
		*end = 0;
		char *fields[6];
		fields[0] = line;
		for (unsigned i = 1; i < 6; i++) {
			char *tab = strchr(fields[i - 1], '\t');
			if (!tab)
				return "invalid index row";
			*tab = 0;
			fields[i] = tab + 1;
		}
		if (strchr(fields[5], '\t'))
			return "invalid index description";
		struct pkg_index_entry *e = &entries[*count];
		if (!copy_field(e->name, sizeof(e->name), fields[0]) ||
		    !copy_field(e->version, sizeof(e->version), fields[1]) ||
		    !copy_field(e->hash, sizeof(e->hash), fields[3]) ||
		    !copy_field(e->filename, sizeof(e->filename), fields[4]) ||
		    !copy_field(e->description, sizeof(e->description), fields[5]))
			return "index field too long";
		if (!pkg_identifier(e->name, sizeof(e->name)) ||
		    !pkg_identifier(e->version, sizeof(e->version)) ||
		    !printable(e->description, sizeof(e->description)))
			return "invalid index metadata";
		uint32_t size = 0;
		const char *s = fields[2];
		if (!*s)
			return "missing package size";
		while (*s) {
			if (*s < '0' || *s > '9' ||
			    size > (PKG_MAX_SIZE - (unsigned)(*s - '0')) / 10)
				return "invalid package size";
			size = size * 10 + (unsigned)(*s++ - '0');
		}
		if (size < PKG_HEADER_SIZE)
			return "invalid package size";
		e->size = size;
		if (strlen(e->hash) != 64)
			return "invalid package digest";
		for (unsigned i = 0; i < 64; i++)
			if (!((e->hash[i] >= '0' && e->hash[i] <= '9') ||
			      (e->hash[i] >= 'a' && e->hash[i] <= 'f')))
				return "invalid package digest";
		char expected[sizeof(e->filename)];
		snprintf(expected, sizeof(expected), "%s-%s.spkg", e->name, e->version);
		if (strcmp(expected, e->filename))
			return "invalid package filename";
		for (size_t i = 0; i < *count; i++)
			if (!strcmp(entries[i].name, e->name))
				return "multiple versions of one package in repository";
		(*count)++;
		line = end + 1;
	}
	return NULL;
}
