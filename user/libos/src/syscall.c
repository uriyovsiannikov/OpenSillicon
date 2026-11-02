#include "internal.h"
#include <errno.h>
int errno;
#define SYS_EXIT 1
#define SYS_WRITE 2
#define SYS_GETPID 3
#define SYS_OPEN 7
#define SYS_CLOSE 8
#define SYS_READ 9
#define SYS_MMAP 21
#define SYS_MUNMAP 22
#define SYS_MPROTECT 23
#define SYS_LSEEK 36
#define SYS_FSTAT 37
#define SYS_GETDENTS 38
#define SYS_FSTATAT 39
extern long __libos_raw_syscall(long,long,long,long,long,long,long);
long __libos_syscall(long n,long a,long b,long c,long d,long e,long f) {
 return __libos_raw_syscall(n,a,b,c,d,e,f);
}
long __libos_call(long n,long a,long b,long c,long d,long e,long f) {
 long r=__libos_syscall(n,a,b,c,d,e,f); if(r<0){errno=(int)-r;if(errno==78)errno=ENOSYS;return -1;} return r;
}
void _exit(int status) { (void)__libos_syscall(SYS_EXIT,status,0,0,0,0,0); for (;;) __asm__ volatile("hlt"); }
void exit(int status) { _exit(status); }
long __libos_getpid(void){return __libos_call(SYS_GETPID,0,0,0,0,0,0);}
long __libos_open(const char*p,int f,unsigned m){return __libos_call(SYS_OPEN,(long)p,f,m,0,0,0);}
long __libos_close(int f){return __libos_call(SYS_CLOSE,f,0,0,0,0,0);}
long __libos_read(int f,void*b,size_t n){return __libos_call(SYS_READ,f,(long)b,(long)n,0,0,0);}
long __libos_write(int f,const void*b,size_t n){return __libos_call(SYS_WRITE,f,(long)b,(long)n,0,0,0);}
long __libos_lseek(int f,off_t o,int w){return __libos_call(SYS_LSEEK,f,o,w,0,0,0);}
long __libos_fstat(int f,void*b){return __libos_call(SYS_FSTAT,f,(long)b,0,0,0,0);}
long __libos_fstatat(int f,const char*p,void*b,int flags){return __libos_call(SYS_FSTATAT,f,(long)p,(long)b,flags,0,0);}
long __libos_getdents(int f,void*b,size_t n){return __libos_call(SYS_GETDENTS,f,(long)b,(long)n,0,0,0);}
void *__libos_mmap(size_t n){long r=__libos_call(SYS_MMAP,n,3,0,0,0,0);return r<0?0:(void*)r;}
void __libos_free_pages(void*p,size_t n){(void)__libos_call(SYS_MUNMAP,(long)p,(long)n,0,0,0,0);}
long __libos_mprotect(void*p,size_t n,int prot){return __libos_call(SYS_MPROTECT,(long)p,n,prot,0,0,0);}
