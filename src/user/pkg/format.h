#ifndef OPEN_SILLICON_PKG_FORMAT_H
#define OPEN_SILLICON_PKG_FORMAT_H
#include <stddef.h>
#include <stdint.h>
#define PKG_MAX_SIZE (16U * 1024U * 1024U)
#define PKG_MAX_FILES 128U
#define PKG_MAX_PACKAGES 256U
#define PKG_HEADER_SIZE 304U
#define PKG_ENTRY_SIZE 232U
#define PKG_NAME_SIZE 64U
#define PKG_VERSION_SIZE 32U
#define PKG_DESCRIPTION_SIZE 192U
#define PKG_PATH_SIZE 192U
#define PKG_INDEX_MAX (256U * 1024U)
#define PKG_ARCHIVE_MAGIC "OSPKG001"
#define PKG_RECORD_MAGIC "OSDB0001"
struct pkg_file {
	const char *path;
	uint32_t mode, size;
	const uint8_t *hash, *data, *header;
};
struct pkg_archive {
	const char *name, *version, *description;
	uint32_t count, installed;
	struct pkg_file files[PKG_MAX_FILES];
};
struct pkg_index_entry {
	char name[PKG_NAME_SIZE], version[PKG_VERSION_SIZE], hash[65];
	char filename[PKG_NAME_SIZE + PKG_VERSION_SIZE + 8],
	    description[PKG_DESCRIPTION_SIZE];
	uint32_t size;
};
uint32_t pkg_u32(const uint8_t *);
void pkg_put_u32(uint8_t *, uint32_t);
int pkg_identifier(const char *, size_t);
int pkg_path(const char *);
/* Parsers return NULL on success, a constant diagnostic on failure. */
const char *pkg_parse(const uint8_t *, size_t, int record, struct pkg_archive *);
const char *pkg_index_parse(char *, struct pkg_index_entry *, size_t *);
#endif
