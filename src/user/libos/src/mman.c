#include <sys/mman.h>
#include <errno.h>
#include "abi/syscall.h"
#include "internal.h"
int munmap(void*p,size_t n){return (int)__libos_call(SYSCALL_MUNMAP,(long)p,n,0,0,0,0);}
int mprotect(void*p,size_t n,int prot){return (int)__libos_call(SYSCALL_MPROTECT,(long)p,n,prot,0,0,0);}
void *mmap(void*p,size_t n,int prot,int flags,int fd,off_t off){(void)p;(void)fd;(void)off;if(flags!=(MAP_PRIVATE|MAP_ANONYMOUS)){errno=EINVAL;return (void *)-1;}return (void *)__libos_call(SYSCALL_MMAP,n,prot,0,0,0,0);}
