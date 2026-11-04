#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
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
 struct stat info;
 if(mode & ~(R_OK|W_OK|X_OK)){errno=EINVAL;return -1;}
 if(stat(path,&info)<0)return -1;
 /* Current processes all run as UID 0. Execute still requires at least
  * one executable permission bit on regular files. */
 if((mode&X_OK)&&!S_ISDIR(info.st_mode)&&!(info.st_mode&0111)){
  errno=EACCES;return -1;
 }
 return 0;
}
