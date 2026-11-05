#ifndef COMPRESSION_H
#define COMPRESSION_H 1

#include <stddef.h>

typedef int     (imlib_decompress_load_f) (const void *fdata,
                                           unsigned int fsize, int dest);

#define IMLIB2_DECOMPRESS_MAX_SIZE (128U * 1024U * 1024U)

int             decompress_load(ImlibImage * im, int load_data,
                                const char *const *pext, int next,
                                imlib_decompress_load_f * fdec);
int             decompress_write(int dest, const void *buf, size_t len,
                                 size_t *written);

#endif                          /* COMPRESSION_H */
