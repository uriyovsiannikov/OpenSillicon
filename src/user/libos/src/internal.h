#ifndef LIBOS_INTERNAL_H
#define LIBOS_INTERNAL_H
#include <sys/types.h>
long __libos_syscall(long, long, long, long, long, long, long);
long __libos_call(long, long, long, long, long, long, long);
void *__libos_mmap(size_t);
void __libos_free_pages(void *, size_t);
#endif
