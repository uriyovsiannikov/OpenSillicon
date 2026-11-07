#ifndef OS_DILLO_ICONV_H
#define OS_DILLO_ICONV_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct os_iconv *iconv_t;
iconv_t iconv_open(const char *, const char *);
size_t iconv(iconv_t, char **, size_t *, char **, size_t *);
int iconv_close(iconv_t);
#ifdef __cplusplus
}
#endif
#endif
