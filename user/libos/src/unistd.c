#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
long __libos_open(const char *, int, unsigned);
long __libos_close(int);
long __libos_read(int, void *, size_t);
long __libos_write(int, const void *, size_t);
long __libos_lseek(int, off_t, int);
long __libos_getpid(void);
int open(const char *p,int f,...){unsigned m=0;if(f&O_CREAT){va_list a;va_start(a,f);m=(unsigned)va_arg(a,int);va_end(a);}return (int)__libos_open(p,f,m);}
int close(int f){return (int)__libos_close(f);}
ssize_t read(int f,void*b,size_t n){return __libos_read(f,b,n);}
ssize_t write(int f,const void*b,size_t n){return __libos_write(f,b,n);}
off_t lseek(int f,off_t o,int w){return __libos_lseek(f,o,w);}
pid_t getpid(void){return (pid_t)__libos_getpid();}
