#ifndef LIBOS_DIRENT_H
#define LIBOS_DIRENT_H
#include <sys/types.h>
#include <stddef.h>
struct dirent { ino_t d_ino; off_t d_off; unsigned short d_reclen; unsigned char d_type; char d_name[256]; };
_Static_assert(offsetof(struct dirent, d_name) == 19, "libos dirent ABI mismatch");
_Static_assert(sizeof(struct dirent) == 280, "libos dirent size mismatch");
typedef struct { int fd; size_t pos, size; unsigned char buffer[4096]; struct dirent entry; } DIR;
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
#endif
