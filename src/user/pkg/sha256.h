#ifndef OPEN_SILLICON_PKG_SHA256_H
#define OPEN_SILLICON_PKG_SHA256_H
#include <stddef.h>
#include <stdint.h>
struct pkg_sha256 {
	uint32_t h[8];
	uint64_t bytes;
	size_t used;
	uint8_t block[64];
};
void pkg_sha256_init(struct pkg_sha256 *);
void pkg_sha256_update(struct pkg_sha256 *, const void *, size_t);
void pkg_sha256_final(struct pkg_sha256 *, uint8_t[32]);
void pkg_sha256(const void *, size_t, uint8_t[32]);
void pkg_hex(const uint8_t[32], char[65]);
#endif
