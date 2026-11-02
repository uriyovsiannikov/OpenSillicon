#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
long __libos_getdents(int,void *,size_t);
DIR *opendir(const char*p){int fd=open(p,O_RDONLY|O_DIRECTORY);DIR*d;if(fd<0)return 0;d=malloc(sizeof(*d));if(!d){close(fd);return 0;}d->fd=fd;d->pos=d->size=0;return d;}
struct dirent *readdir(DIR*d){if(!d)return 0;for(;;){if(d->pos<d->size){unsigned char*b=d->buffer+d->pos;unsigned short reclen=*(unsigned short*)(b+16);size_t nameoff=19,n; if(reclen<nameoff+1||d->pos+reclen>d->size)return 0;n=0;while(nameoff+n<reclen&&b[nameoff+n])n++;if(nameoff+n>=reclen||n>=sizeof(d->entry.d_name))return 0;d->entry.d_ino=*(unsigned long long*)b;d->entry.d_off=*(long long*)(b+8);d->entry.d_reclen=reclen;d->entry.d_type=b[18];memcpy(d->entry.d_name,b+nameoff,n+1);d->pos+=reclen;return &d->entry;}long got=__libos_getdents(d->fd,d->buffer,sizeof(d->buffer));if(got<=0)return 0;d->pos=0;d->size=(size_t)got;}}
int closedir(DIR*d){int r;if(!d)return -1;r=close(d->fd);free(d);return r;}
