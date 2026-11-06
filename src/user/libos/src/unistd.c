#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <limits.h>
#include "abi/syscall.h"
#include "abi/credentials.h"
#include "internal.h"

char *
getcwd(char *buffer, size_t size)
{
	long result;
	if (buffer == NULL || size < 2) {
		errno = buffer == NULL ? EINVAL : ERANGE;
		return NULL;
	}
	result = __libos_call(SYSCALL_GETCWD, (long)buffer, (long)size,
	    0, 0, 0, 0);
	if (result < 0)
		return NULL;
	return buffer;
}

int
chdir(const char *path)
{
	if (path == NULL) {
		errno = EFAULT;
		return -1;
	}
	return (int)__libos_call(SYSCALL_CHDIR, (long)path, 0, 0, 0, 0, 0);
}
int mkdir(const char *path, unsigned int mode)
{
	if (path == NULL) { errno = EFAULT; return -1; }
	return (int)__libos_call(SYSCALL_MKDIR, (long)path, mode, 0, 0, 0, 0);
}
int rmdir(const char *path){(void)path;errno=ENOSYS;return -1;}

int dup(int fd){return (int)__libos_call(SYSCALL_DUP,fd,0,0,0,0,0);}
pid_t wait(int *status){return waitpid(-1,status,0);}
int setpgrp(void){errno=ENOSYS;return -1;}
char *ttyname(int fd){(void)fd;errno=ENOTTY;return NULL;}
unsigned int alarm(unsigned int seconds){(void)seconds;errno=ENOSYS;return 0;}
int chown(const char *path,uid_t owner,gid_t group){return (int)__libos_call(SYSCALL_CHOWN,(long)path,owner,group,0,0,0);}
int fchown(int fd,uid_t owner,gid_t group){return (int)__libos_call(SYSCALL_FCHOWN,fd,owner,group,0,0,0);}
int chmod(const char *path,unsigned int mode){return (int)__libos_call(SYSCALL_CHMOD,(long)path,mode,0,0,0,0);}
void endgrent(void){}

int
getpagesize(void)
{
	return (int)sysconf(_SC_PAGESIZE);
}

int
fchmod(int descriptor, unsigned int mode)
{
	return (int)__libos_call(SYSCALL_FCHMOD, descriptor, mode, 0, 0, 0, 0);
}

int
isatty(int fd)
{
	struct winsize size;
	return ioctl(fd, TIOCGWINSZ, &size) == 0;
}

int
putenv(char *assignment)
{
	(void)assignment;
	errno = ENOSYS;
	return -1;
}

int
execvp(const char *file, char *const argv[])
{
	static const char *paths[] = { "/bin/", "/sbin/" };
	char candidate[256];
	size_t i, len;
	if (file == NULL || *file == '\0' || argv == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (strchr(file, '/') != NULL)
		return execv(file, argv);
	len = strlen(file);
	if (len > sizeof(candidate) - 8) {
		errno = ENAMETOOLONG;
		return -1;
	}
	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		strcpy(candidate, paths[i]);
		strcat(candidate, file);
		(void)execv(candidate, argv);
		if (errno != ENOENT && errno != EACCES)
			return -1;
	}
	errno = ENOENT;
	return -1;
}

int
execlp(const char *file, const char *arg, ...)
{
	char *arguments[32];
	va_list ap;
	size_t count = 0;
	if (arg == NULL) {
		errno = EINVAL;
		return -1;
	}
	arguments[count++] = (char *)arg;
	va_start(ap, arg);
	while (count < sizeof(arguments) / sizeof(arguments[0])) {
		char *next = va_arg(ap, char *);
		arguments[count++] = next;
		if (next == NULL)
			break;
	}
	va_end(ap);
	if (arguments[count - 1] != NULL) {
		errno = E2BIG;
		return -1;
	}
	return execvp(file, arguments);
}

int
mkstemps(char *path, int suffix_length)
{
	static unsigned int sequence;
	static const char digits[] = "0123456789abcdef";
	size_t length, i;
	unsigned int value;
	int fd;
	if (path == NULL || suffix_length < 0) {
		errno = EINVAL;
		return -1;
	}
	length = strlen(path);
	if ((size_t)suffix_length > length || length - suffix_length < 6 ||
	    strncmp(path + length - suffix_length - 6, "XXXXXX", 6) != 0) {
		errno = EINVAL;
		return -1;
	}
	for (unsigned int attempt = 0; attempt < 256; attempt++) {
		value = ++sequence ^ ((unsigned int)getpid() << 8);
		for (i = 0; i < 6; i++) {
			path[length - suffix_length - 1 - i] = digits[value & 15U];
			value >>= 4;
		}
		fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0 || errno != EEXIST)
			return fd;
	}
	return -1;
}

int mkstemp(char *path) { return mkstemps(path, 0); }

int
usleep(unsigned int microseconds)
{
	struct timespec request = {
		.tv_sec = microseconds / 1000000U,
		.tv_nsec = (long)(microseconds % 1000000U) * 1000L
	};
	return nanosleep(&request, NULL);
}
long __libos_open(const char *, int, unsigned);
mode_t umask(mode_t mask){return (mode_t)__libos_call(SYSCALL_UMASK,mask,0,0,0,0,0);}
long __libos_close(int);
long __libos_read(int, void *, size_t);
long __libos_write(int, const void *, size_t);
long __libos_lseek(int, off_t, int);
long __libos_getpid(void);
int open(const char *p,int f,...) {
 unsigned m=0;
 char absolute[PATH_MAX];
 if(p==NULL){errno=EFAULT;return -1;}
 if(*p=='\0'){errno=ENOENT;return -1;}
 if(f&O_APPEND){errno=EOPNOTSUPP;return -1;}
 if(f&O_CREAT){va_list a;va_start(a,f);m=(unsigned)va_arg(a,int);va_end(a);}
 /* The native open syscall requires an absolute path. */
 if(p[0]!='/') {
  if(getcwd(absolute,sizeof(absolute))==NULL)return -1;
  size_t length=strlen(absolute), size=strlen(p);
  int separator=length>0&&absolute[length-1]!='/';
  if(size>=sizeof(absolute)-length-(size_t)separator){errno=ENAMETOOLONG;return -1;}
  if(separator)absolute[length++]='/';
  memcpy(absolute+length,p,size+1);
  p=absolute;
 }
 return (int)__libos_open(p,f,m);
}
int close(int f){return (int)__libos_close(f);}
int dup2(int source, int destination){return (int)__libos_call(SYSCALL_DUP2,source,destination,0,0,0,0);}
ssize_t read(int f,void*b,size_t n){return __libos_read(f,b,n);}
ssize_t write(int f,const void*b,size_t n){return __libos_write(f,b,n);}
off_t lseek(int f,off_t o,int w){return __libos_lseek(f,o,w);}
pid_t getpid(void){return (pid_t)__libos_getpid();}
int unlink(const char *path){(void)path;errno=EOPNOTSUPP;return -1;}
pid_t getppid(void){return (pid_t)__libos_call(SYSCALL_GETPPID,0,0,0,0,0,0);}
long sysconf(int name){switch(name){case _SC_OPEN_MAX:return 32;case _SC_PAGESIZE:return 4096;case _SC_IOV_MAX:return 16;default:errno=EINVAL;return -1;}}
static int
get_credentials(struct os_credentials *credentials)
{
	long result = __libos_call(SYSCALL_GETCREDENTIALS,
	    (long)credentials, 0, 0, 0, 0, 0);
	return result < 0 ? -1 : 0;
}

uid_t getuid(void){struct os_credentials c;return get_credentials(&c)==0?c.real_uid:0;}
uid_t geteuid(void){struct os_credentials c;return get_credentials(&c)==0?c.effective_uid:0;}
gid_t getgid(void){struct os_credentials c;return get_credentials(&c)==0?c.real_gid:0;}
gid_t getegid(void){struct os_credentials c;return get_credentials(&c)==0?c.effective_gid:0;}
int getresuid(uid_t *real,uid_t *effective,uid_t *saved){struct os_credentials c;if(!real||!effective||!saved){errno=EFAULT;return -1;}if(get_credentials(&c)<0)return -1;*real=c.real_uid;*effective=c.effective_uid;*saved=c.saved_uid;return 0;}
int getresgid(gid_t *real,gid_t *effective,gid_t *saved){struct os_credentials c;if(!real||!effective||!saved){errno=EFAULT;return -1;}if(get_credentials(&c)<0)return -1;*real=c.real_gid;*effective=c.effective_gid;*saved=c.saved_gid;return 0;}
int setuid(uid_t id){return (int)__libos_call(SYSCALL_SETUID,id,0,0,0,0,0);}
int seteuid(uid_t id){return (int)__libos_call(SYSCALL_SETEUID,id,0,0,0,0,0);}
int setgid(gid_t id){return (int)__libos_call(SYSCALL_SETGID,id,0,0,0,0,0);}
int setegid(gid_t id){return (int)__libos_call(SYSCALL_SETEGID,id,0,0,0,0,0);}
int setresuid(uid_t real,uid_t effective,uid_t saved){return (int)__libos_call(SYSCALL_SETRESUID,real,effective,saved,0,0,0);}
int setresgid(gid_t real,gid_t effective,gid_t saved){return (int)__libos_call(SYSCALL_SETRESGID,real,effective,saved,0,0,0);}
pid_t fork(void){return (pid_t)__libos_call(SYSCALL_FORK,0,0,0,0,0,0);}
pid_t waitpid(pid_t pid,int *status,int options){
	unsigned char child_status=0;
	long result;
	if(options!=0){errno=EINVAL;return -1;}
	result=__libos_call(SYSCALL_WAITPID,pid,(long)(status?&child_status:0),0,0,0,0);
	if(result>=0&&status)*status=(int)child_status<<8;
	return (pid_t)result;
}
int execv(const char *path,char *const argv[]){
	long argc=0;
	while(argv!=0&&argc<16&&argv[argc]!=0)argc++;
	if(argc==0){errno=EINVAL;return -1;}
	if(argc==16&&argv[argc]!=0){errno=E2BIG;return -1;}
	return (int)__libos_call(SYSCALL_EXEC_PATH_ARGV,(long)path,argc,(long)argv,0,0,0);
}
int execl(const char *path,const char *arg0,...){
	char *arguments[17];
	long argc=0;
	va_list args;
	if(arg0==0){errno=EINVAL;return -1;}
	arguments[argc++]=(char *)arg0;
	va_start(args,arg0);
	while(argc<16){
		char *argument=va_arg(args,char *);
		if(argument==0)break;
		arguments[argc++]=argument;
	}
	if(argc==16&&arguments[argc-1]!=0){
		char *extra=va_arg(args,char *);
		if(extra!=0){va_end(args);errno=E2BIG;return -1;}
	}
	va_end(args);
	arguments[argc]=0;
	return (int)__libos_call(SYSCALL_EXEC_PATH_ARGV,(long)path,argc,(long)arguments,0,0,0);
}
