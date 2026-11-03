#ifndef KERNEL_FD_H
#define KERNEL_FD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD_TABLE_CAPACITY 32U
#define FD_POLL_READ 0x01U
#define FD_POLL_WRITE 0x02U
#define FD_POLL_ERROR 0x04U
#define FD_STATUS_NONBLOCK 0x00000800U
#define FD_DESCRIPTOR_CLOEXEC 0x00000001U

struct vfs_dirent;

struct fd_metadata {
	uint64_t size;
	uint64_t inode;
	uint32_t mode;
	uint32_t type;
};

struct fd_operations {
	int (*read)(void *context, uint64_t offset, void *buffer, size_t length,
	    size_t *read_length);
	int (*write)(void *context, uint64_t offset, const void *buffer,
	    size_t length, size_t *write_length);
	int (*readdir)(void *context, uint64_t index, struct vfs_dirent *entry);
	int (*seek)(void *context, uint64_t current, int64_t offset,
	    int whence, uint64_t *result);
	int (*stat)(void *context, struct fd_metadata *metadata);
	int (*get_path)(void *context, char *path, size_t capacity);
	bool (*is_terminal)(void *context);
	uint32_t (*poll)(void *context, uint32_t events);
	void (*close)(void *context);
};

struct fd_object {
	const struct fd_operations *operations;
	void *context;
	uint64_t offset;
	uint32_t references;
	uint32_t status_flags;
	bool immortal;
};

struct fd_table {
	struct fd_object *entries[FD_TABLE_CAPACITY];
	uint32_t descriptor_flags[FD_TABLE_CAPACITY];
};

struct fd_object *fd_object_create(const struct fd_operations *operations,
	void *context);
void fd_object_get(struct fd_object *object);
void fd_object_put(struct fd_object *object);
int fd_object_read(struct fd_object *object, void *buffer, size_t length,
	size_t *read_length);
int fd_object_write(struct fd_object *object, const void *buffer,
	size_t length, size_t *write_length);
void fd_table_init_standard(struct fd_table *table);
void fd_console_framebuffer_enable(bool enable);
bool fd_table_clone(struct fd_table *destination,
	const struct fd_table *source);
void fd_table_destroy(struct fd_table *table);
int fd_install(struct fd_table *table, struct fd_object *object);
int fd_install_pair(struct fd_table *table, struct fd_object *first,
	struct fd_object *second, int *first_descriptor,
	int *second_descriptor);
struct fd_object *fd_object_for_descriptor(struct fd_table *table,
	uint64_t descriptor);
int fd_get_descriptor_flags(struct fd_table *table, uint64_t descriptor,
	uint32_t *flags);
int fd_set_descriptor_flags(struct fd_table *table, uint64_t descriptor,
	uint32_t flags);
void fd_table_close_on_exec(struct fd_table *table);
bool fd_object_is_terminal(const struct fd_object *object);
bool fd_object_is_console(const struct fd_object *object);
uint32_t fd_object_poll(struct fd_object *object, uint32_t events);
bool fd_object_nonblocking(const struct fd_object *object);
void fd_readiness_wake(void);
void *fd_readiness_wait_channel(void);
int fd_close(struct fd_table *table, uint64_t descriptor);
int fd_dup(struct fd_table *table, uint64_t source);
int fd_dup2(struct fd_table *table, uint64_t source, uint64_t destination);
int fd_read(struct fd_table *table, uint64_t descriptor, void *buffer,
	size_t length, size_t *read_length);
int fd_write(struct fd_table *table, uint64_t descriptor, const void *buffer,
	size_t length, size_t *write_length);
int fd_readdir(struct fd_table *table, uint64_t descriptor,
	struct vfs_dirent *entry);
int fd_object_seek(struct fd_object *object, int64_t offset, int whence,
	uint64_t *result);
int fd_seek(struct fd_table *table, uint64_t descriptor, int64_t offset,
	int whence, uint64_t *result);
int fd_object_stat(struct fd_object *object, struct fd_metadata *metadata);
int fd_object_get_path(struct fd_object *object, char *path, size_t capacity);
uint64_t fd_get_offset(const struct fd_table *table, uint64_t descriptor);
bool fd_set_offset(struct fd_table *table, uint64_t descriptor,
	uint64_t offset);

#endif
