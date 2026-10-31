#ifndef KERNEL_FD_H
#define KERNEL_FD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD_TABLE_CAPACITY 32U

struct vfs_dirent;

struct fd_operations {
	int (*read)(void *context, uint64_t offset, void *buffer, size_t length,
	    size_t *read_length);
	int (*write)(void *context, uint64_t offset, const void *buffer,
	    size_t length, size_t *write_length);
	int (*readdir)(void *context, uint64_t index, struct vfs_dirent *entry);
	bool (*is_terminal)(void *context);
	void (*close)(void *context);
};

struct fd_object {
	const struct fd_operations *operations;
	void *context;
	uint64_t offset;
	uint32_t references;
	bool immortal;
};

struct fd_table {
	struct fd_object *entries[FD_TABLE_CAPACITY];
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
bool fd_object_is_terminal(const struct fd_object *object);
int fd_close(struct fd_table *table, uint64_t descriptor);
int fd_read(struct fd_table *table, uint64_t descriptor, void *buffer,
	size_t length, size_t *read_length);
int fd_write(struct fd_table *table, uint64_t descriptor, const void *buffer,
	size_t length, size_t *write_length);
int fd_readdir(struct fd_table *table, uint64_t descriptor,
	struct vfs_dirent *entry);
uint64_t fd_get_offset(const struct fd_table *table, uint64_t descriptor);
bool fd_set_offset(struct fd_table *table, uint64_t descriptor,
	uint64_t offset);

#endif
