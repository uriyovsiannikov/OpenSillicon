#ifndef KERNEL_PTY_H
#define KERNEL_PTY_H

#include "kernel/fd.h"

int pty_create(struct fd_table *table, int *master, int *slave);
bool pty_is_object(const struct fd_object *object);
int pty_set_raw(struct fd_object *object, bool enabled);
int pty_bytes_available(struct fd_object *object, size_t *available);
int pty_window(struct fd_object *object, uint16_t dimensions[4], bool set);
int pty_termios(struct fd_object *object, void *attributes, bool set);

#endif
