#ifndef OPEN_SILLICON_PKG_IO_H
#define OPEN_SILLICON_PKG_IO_H
#include <stddef.h>
#include <stdint.h>
int pkg_write_all(int, const void *, size_t);
uint8_t *pkg_read_file(const char *, size_t, size_t *);
int pkg_mkdirs(const char *);
#endif
