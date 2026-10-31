#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

/* Linux/POSIX numeric errno values used by the current kernel interfaces. */
#define EIO 5
#define E2BIG 7
#define EBADF 9
#define ENOTTY 25
#define ENOMEM 12
#define EFAULT 14
#define EINVAL 22
#define EEXIST 17
#define EAGAIN 11
#define EMFILE 24
#define ENOSPC 28
#define EPIPE 32
#define EOVERFLOW 75
#define EOPNOTSUPP 95
#define ETIMEDOUT 110

#endif
