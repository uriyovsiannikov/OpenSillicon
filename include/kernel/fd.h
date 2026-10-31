#ifndef KERNEL_FD_H
#define KERNEL_FD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FD_TABLE_CAPACITY 3U

struct file_operations {
	bool (*write)(const char *buffer, size_t length);
};

struct file_descriptor {
	const struct file_operations *operations;
};

struct fd_table {
	struct file_descriptor entries[FD_TABLE_CAPACITY];
};

void fd_table_init_standard(struct fd_table *table);
bool fd_write(struct fd_table *table, uint64_t descriptor,
	const char *buffer, size_t length);

#endif
