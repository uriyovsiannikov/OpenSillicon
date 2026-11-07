#ifndef OPEN_SILLICON_PKG_HTTP_H
#define OPEN_SILLICON_PKG_HTTP_H
#include <stddef.h>
#include <stdint.h>
uint8_t *pkg_http_get(const char *repository, const char *path, size_t limit,
		      size_t *length);
#endif
