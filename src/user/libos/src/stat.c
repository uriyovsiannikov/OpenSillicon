#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "abi/syscall.h"
#include "internal.h"
long __libos_fstat(int,void *);
long __libos_fstatat(int,const char *,void *,int);
int fstat(int fd,struct stat *s){return (int)__libos_fstat(fd,s);}
int fstatat(int fd,const char*p,struct stat*s,int flags){return (int)__libos_fstatat(fd,p,s,flags);}
int stat(const char*p,struct stat*s){return fstatat(AT_FDCWD,p,s,0);}
int futimens(int fd, const struct timespec times[2])
{
 (void)fd; (void)times; errno=EOPNOTSUPP; return -1;
}
int access(const char *path, int mode)
{
 if(mode & ~(R_OK|W_OK|X_OK)){errno=EINVAL;return -1;}
	return (int)__libos_call(SYSCALL_ACCESS,(long)path,mode,0,0,0,0);
}
