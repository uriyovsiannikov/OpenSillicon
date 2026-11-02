#include <fcntl.h>
#include <sys/stat.h>
long __libos_fstat(int,void *);
long __libos_fstatat(int,const char *,void *,int);
int fstat(int fd,struct stat *s){return (int)__libos_fstat(fd,s);}
int fstatat(int fd,const char*p,struct stat*s,int flags){return (int)__libos_fstatat(fd,p,s,flags);}
int stat(const char*p,struct stat*s){return fstatat(AT_FDCWD,p,s,0);}
