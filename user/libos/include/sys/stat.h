#ifndef LIBOS_SYS_STAT_H
#define LIBOS_SYS_STAT_H
#include <sys/types.h>
#include <stddef.h>
typedef unsigned long long dev_t;
typedef unsigned int mode_t;
typedef unsigned long nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long blksize_t;
typedef long long blkcnt_t;
struct timespec { long tv_sec; long tv_nsec; };
struct stat {
 dev_t st_dev; ino_t st_ino; nlink_t st_nlink; mode_t st_mode;
 uid_t st_uid; gid_t st_gid; int pad0; dev_t st_rdev;
 off_t st_size; blksize_t st_blksize; blkcnt_t st_blocks;
 struct timespec st_atim, st_mtim, st_ctim;
 long __reserved[3];
};
_Static_assert(sizeof(struct stat) == 144, "libos stat ABI size mismatch");
_Static_assert(offsetof(struct stat, st_mode) == 24, "libos stat mode offset mismatch");
_Static_assert(offsetof(struct stat, st_size) == 48, "libos stat size offset mismatch");
#define S_IFMT 0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000
#define S_IFCHR 0020000
#define S_IFBLK 0060000
#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
int fstat(int, struct stat *);
int stat(const char *, struct stat *);
#endif
