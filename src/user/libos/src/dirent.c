#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
long __libos_getdents(int,void *,size_t);
DIR *opendir(const char*p){int fd=open(p,O_RDONLY|O_DIRECTORY);DIR*d;if(fd<0)return 0;d=malloc(sizeof(*d));if(!d){close(fd);return 0;}d->fd=fd;d->pos=d->size=0;return d;}
struct dirent *readdir(DIR*d){if(!d)return 0;for(;;){if(d->pos<d->size){unsigned char*b=d->buffer+d->pos;unsigned short reclen=*(unsigned short*)(b+16);size_t nameoff=19,n; if(reclen<nameoff+1||d->pos+reclen>d->size)return 0;n=0;while(nameoff+n<reclen&&b[nameoff+n])n++;if(nameoff+n>=reclen||n>=sizeof(d->entry.d_name))return 0;d->entry.d_ino=*(unsigned long long*)b;d->entry.d_off=*(long long*)(b+8);d->entry.d_reclen=reclen;d->entry.d_type=b[18];memcpy(d->entry.d_name,b+nameoff,n+1);d->pos+=reclen;return &d->entry;}long got=__libos_getdents(d->fd,d->buffer,sizeof(d->buffer));if(got<=0)return 0;d->pos=0;d->size=(size_t)got;}}
int closedir(DIR*d){int r;if(!d)return -1;r=close(d->fd);free(d);return r;}
void rewinddir(DIR *directory)
{
	if (directory != NULL && lseek(directory->fd, 0, SEEK_SET) >= 0)
		directory->pos = directory->size = 0;
}

static int
compare_directory_entries(const struct dirent **left, const struct dirent **right)
{
	return strcmp((*left)->d_name, (*right)->d_name);
}

int
alphasort(const struct dirent **left, const struct dirent **right)
{
	return compare_directory_entries(left, right);
}

int
scandir(const char *path, struct dirent ***entries_out,
	int (*select_entry)(const struct dirent *),
	int (*compare_entries)(const struct dirent **, const struct dirent **))
{
	DIR *directory;
	struct dirent **entries = NULL;
	size_t count = 0, capacity = 0;
	struct dirent *entry;
	if (path == NULL || entries_out == NULL) { errno = EINVAL; return -1; }
	directory = opendir(path);
	if (directory == NULL) return -1;
	while ((entry = readdir(directory)) != NULL) {
		struct dirent *copy;
		if (select_entry != NULL && !select_entry(entry)) continue;
		copy = malloc(sizeof(*copy));
		if (copy == NULL) goto fail;
		*copy = *entry;
		if (count == capacity) {
			size_t next_capacity = capacity == 0 ? 16 : capacity * 2;
			struct dirent **next = realloc(entries,
			    next_capacity * sizeof(*entries));
			if (next == NULL) { free(copy); goto fail; }
			entries = next;
			capacity = next_capacity;
		}
		entries[count++] = copy;
	}
	closedir(directory);
	if (compare_entries != NULL)
		qsort(entries, count, sizeof(*entries),
		    (int (*)(const void *, const void *))compare_entries);
	*entries_out = entries;
	return (int)count;
fail:
	for (size_t i = 0; i < count; i++) free(entries[i]);
	free(entries);
	closedir(directory);
	errno = ENOMEM;
	return -1;

}
