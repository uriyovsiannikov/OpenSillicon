#ifndef LIBOS_PTY_H
#define LIBOS_PTY_H

/* Allocate an unnamed master/slave pair. Named devpts and POSIX termios
 * configuration are not yet supported; pass NULL for name and attributes. */
struct termios;
struct winsize;
int openpty(int *master, int *slave, char *name,
    const struct termios *attributes, const struct winsize *size);

#endif
