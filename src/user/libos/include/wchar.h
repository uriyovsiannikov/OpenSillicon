#ifndef LIBOS_WCHAR_H
#define LIBOS_WCHAR_H
#include <stddef.h>
size_t wcslen(const wchar_t *);
wchar_t *wcscpy(wchar_t *, const wchar_t *);
wchar_t *wcsncpy(wchar_t *, const wchar_t *, size_t);
#endif
