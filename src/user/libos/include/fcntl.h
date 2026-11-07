#ifndef LIBOS_FCNTL_H
#define LIBOS_FCNTL_H
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_ACCMODE 3
#define O_CREAT 0x40
#define O_EXCL 0x80
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_DIRECTORY 0x10000
/* The VFS has no symbolic-link vnode type, so open cannot follow one. */
#define O_NOFOLLOW 0
#define O_NONBLOCK 0x800
#define O_NDELAY O_NONBLOCK
#define FNDELAY O_NONBLOCK
#define FASYNC 0x2000
#define F_GETFL 3
#define F_DUPFD 0
#define F_SETFL 4
#define F_GETFD 1
#define F_SETFD 2
#define FD_CLOEXEC 1
#define F_SETOWN 8
#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
struct stat;
int open(const char *, int, ...);
int fcntl(int, int, ...);
int fstatat(int, const char *, struct stat *, int);
#endif
