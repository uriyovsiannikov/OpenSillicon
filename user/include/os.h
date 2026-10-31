#ifndef OPENSILLICON_USER_OS_H
#define OPENSILLICON_USER_OS_H

#include <stddef.h>
#include <stdint.h>

#define U_O_RDONLY 0U
#define U_O_WRONLY 1U
#define U_O_DIRECTORY 0x10000U

struct u_stat {
	uint64_t size;
	uint32_t mode;
	uint32_t type;
};

struct u_dirent {
	uint64_t inode;
	uint64_t size;
	uint32_t mode;
	uint32_t type;
	char name[256];
};

long u_write(uint64_t fd, const void *buffer, size_t length);
long u_read(uint64_t fd, void *buffer, size_t length);
long u_open(const char *path, uint64_t flags);
long u_close(uint64_t fd);
long u_stat_path(const char *path, struct u_stat *status);
long u_readdir(uint64_t fd, struct u_dirent *entry);
long u_fork(void);
long u_exec_path(const char *path);
long u_waitpid(uint64_t pid, uint8_t *status);
long u_sleep_ticks(uint64_t ticks);
long u_pipe(uint32_t descriptors[2]);
_Noreturn void u_exit(uint64_t status);
size_t u_strlen(const char *string);
int u_streq(const char *left, const char *right);
void u_puts(const char *string);
void u_putn(const char *string, size_t length);
void u_put_u64(uint64_t value);
int u_normalize_path(const char *cwd, const char *path, char *output,
	size_t capacity);

#endif
