#ifndef ABI_IOCTL_H
#define ABI_IOCTL_H

/* Byte count currently queued on a Unix stream socket. */
#define OS_FIONREAD 0x541BU
#define OS_TCGETS 0x5401U
#define OS_TCSETS 0x5402U
/* Temporarily suppress framebuffer rendering of console output for a GUI session. */
#define OS_TIOCSFB 0x4f530003U
/* Claim (1) or release (0) raw input for the active console graphics session. */
#define OS_TIOCSINPUT 0x4f530004U

#endif
